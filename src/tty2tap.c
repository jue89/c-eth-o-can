#include <sys/select.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include "crc16.h"
#include "debug.h"
#include "config.h"
#include "tty2tap.h"

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

void tty2tap (int ttyFd, int tapFd) {
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
