#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include "crc16.h"
#include "debug.h"
#include "config.h"

static void flushPendingData (int fd) {
	int rc;
	char c;
	do {
		rc = read(fd, &c, 1);
		if (rc) DEBUG("--> FLUSH 0x%02x\n", c);
	} while (rc > 0);
}

static int sendRawOctet (int fd, char octet) {
	int rc;
	fd_set rfds;
	struct timeval timeout;
	char echo;

	// Write byte
	rc = write(fd, &octet, 1);
	if (rc == 0) {
		errno = EBUSY;
		return -1;
	} else if (rc < 0) {
		// Some other error
		// errno will be set correctly
		return -1;
	}
	DEBUG("SND 0x%02x ... ", octet);

	// Wait up to 5ms for echo
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	timeout.tv_sec = 0;
	timeout.tv_usec = OCTET_TIMEOUT;
	rc = select(fd + 1, &rfds, NULL, NULL, &timeout);
	if (rc == 0) {
		// Timeout
		errno = ETIMEDOUT;
		return -1;
	} else if (rc < 0) {
		// Some other error
		// errno will be set correctly
		return -1;
	}

	// Data is readable -> check the echo
	rc = read(fd, &echo, 1);
	if (rc != 1) {
		errno = ETIMEDOUT;
		return -1;
	}
	DEBUG("RCV 0x%02x\n", echo);

	if (octet != echo) {
		// Someone interrupted the connection
		errno = ECOMM;
		return -1;
	}

	return 0;
}

static int sendOctet (int fd, char octet) {
	// Mask END and ESC characters
	if (octet == C_END || octet == C_ESC) {
		if (sendRawOctet(fd, (char) C_ESC)) return -1;
	}

	// Send current octet
	if (sendRawOctet(fd, octet)) return -1;

	return 0;
}

static ssize_t sendData (int fd, const char * buf, size_t count) {
	int i;
	uint16_t crc = 0xffff;
	ssize_t sentBytes = 0;

	// Make sure no old data is left in read queue
	flushPendingData(fd);

	// Send each data byte
	DEBUG("--> START OF FRAME\n");
	for (i = 0; i < count; i++) {
		char octet = buf[i];

		// Bring data on the wire
		if (sendOctet(fd, octet)) return -1;

		// Update CRC
		crc = crc16Update(crc, octet);

		sentBytes++;
	}

	// Send CRC
	DEBUG("--> CRC 0x%04x\n", crc);
	crc = htons(crc);
	if (sendOctet(fd, (char) (crc >> 0))) return -1;
	if (sendOctet(fd, (char) (crc >> 8))) return -1;


	// Send end of frame
	if(sendRawOctet(fd, (char) C_END)) return -1;
	DEBUG("--> END OF FRAME\n");

	return sentBytes;
}

static void tap2tty (int tapFd, int ttyFd) {
	char buf[MTU];
	ssize_t n;
	int tries = 3;

	n = read(tapFd, buf, sizeof(buf));
	if (n < 0) {
		perror("read from tap device");
		return;
	}

retry:
	if (sendData(ttyFd, buf, n) < 0) {
		perror("write to tty device");
		printf("TAP > TTY: %zu bytes ...", n);
		if (tries-- > 0) {
			int delay = rand() & 0xfff; // 0..4095us
			delay += 2 * OCTET_TIMEOUT;
			printf(" failed. Retry in %d us.\n", delay);
			usleep(delay);
			goto retry;
		} else {
			printf(" failed. Drop.\n");
		}
	} else {
		printf("TAP > TTY: %zu bytes ... sent.\n", n);
	}
}

static int readData(int fd, char *buf, size_t bufSize) {
	int rc;
	size_t n = 0;
	int esc = 0;
	uint16_t crc = 0xffff;
	fd_set rfds;
	struct timeval timeout;

	DEBUG("--> START OF FRAME");
readOctet:
	// Make sure we have enough room for the message
	if (n >= bufSize) {
		errno = ENOBUFS;
		return -1;
	}

	// Wait up to 5ms for data
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	timeout.tv_sec = 0;
	timeout.tv_usec = OCTET_TIMEOUT;
	rc = select(fd + 1, &rfds, NULL, NULL, &timeout);
	if (rc == 0) {
		// Timeout
		errno = ETIMEDOUT;
		return -1;
	} else if (rc < 0) {
		// Some other error
		// errno will be set correctly
		return -1;
	}

	rc = read(fd, buf + n, 1);
	if (rc < 0) {
		return -1;
	} else if (rc == 0) {
		// TODO: Find good errno
		return -1;
	}
	DEBUG("RCV 0x%02x (%04d)\n", buf[n], n);

	if (esc) {
		// If the last received character was C_ESC
		// just read the next octet from the line
		esc = 0;
	} else if (buf[n] == C_ESC) {
		esc = 1;
		goto readOctet;
	} else if (buf[n] == C_END) {
		// End of frame
		if (crc != 0x0000) {
			// CRC error
			errno = EIO;
			return -1;
		}

		DEBUG("--> START OF FRAME");
		// Remove CRC from received packet
		return n - 2;
	}

	// Received data octet
	crc = crc16Update(crc, buf[n]);
	n++;
	goto readOctet;
}

static void tty2tap (int ttyFd, int tapFd) {
	char buf[MTU];
	ssize_t n;

	n = readData(ttyFd, buf, sizeof(buf));
	if (n < 0) {
		perror("read from tty device");
		return;
	}

	if (write(tapFd, buf, n) < 0) {
		perror("write to tap device");
	}

	printf("TAP < TTY: %zu bytes reveived.\n", n);
}

void loopEmulated (int ttyFd, int tapFd, int senseFd) {
	static int maxFd = 0;
	int rc;
	fd_set rfds;

	// Search highest file descriptor
	if (!maxFd) {
		maxFd = ttyFd;
		if (maxFd < tapFd) maxFd = tapFd;
		maxFd++;
	}

	// Wait for incoming data
	while (1) {
		FD_ZERO(&rfds);
		FD_SET(tapFd, &rfds);
		FD_SET(ttyFd, &rfds);
		rc = select(maxFd, &rfds, NULL, NULL, NULL);
		if (rc <= 0) {
			return;
		}

		if (FD_ISSET(ttyFd, &rfds)) {
			// Data from TTY device
			tty2tap(ttyFd, tapFd);
		} else if (FD_ISSET(tapFd, &rfds)) {
			// Data from TAP device
			tap2tty(tapFd, ttyFd);
		}
	}
}
