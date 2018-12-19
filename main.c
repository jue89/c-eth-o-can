#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define DEBUG(f_, ...) printf((f_), __VA_ARGS__)

int sendData (int fd, char * buf, size_t count) {
	int rc;
	char c;
	size_t i, sentBytes;

	// Flush pending data
	do {
		rc = read(fd, &c, 1);
		if (rc) DEBUG("--> Flush 0x%02x\n", c);
	} while (rc > 0);

	// Send each data byte
	sentBytes = 0;
	for (i = 0; i < count; i++) {
		fd_set rfds;
		struct timeval timeout;

		// Write byte
		rc = write(fd, buf + i, 1);
		if (rc == 0) {
			errno = EBUSY;
			return -1;
		} else if (rc < 0) {
			return -1;
		}
		DEBUG("SND 0x%02x ... ", buf[i]);

		// Wait up to 10ms for echo
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		rc = select(fd + 1, &rfds, NULL, NULL, &timeout);
		if (rc == 0) {
			// Timeout
			errno = ETIMEDOUT;
			return -1;
		} else if (rc < 0) {
			// Some other error
			return -1;
		} else {
			// Data is readable -> check the echo
			rc = read(fd, &c, 1);
			if (rc != 1) {
				errno = ETIMEDOUT;
				return -1;
			}
			DEBUG("RCV 0x%02x\n", c);
			if (buf[i] != c) {
				// Someone interrupted the connection
				errno = ECOMM;
				return -1;
			}

			// Write was successful!
			sentBytes++;
		}
	}

	return sentBytes;
}

int main (int argc, char **argv) {
	int rc;
	int fd;
	struct termios tty;

	fd = open ("/dev/ttyAMA0", O_RDWR | O_NOCTTY | O_NONBLOCK);

	// Setup tty device
	memset(&tty, 0, sizeof(tty));
	tcgetattr(fd, &tty);
	cfmakeraw(&tty);
	tty.c_cc[VMIN]  = 1; // One input byte is enough to return from read()
	tty.c_cc[VTIME] = 0;
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);
	tcsetattr(fd, TCSANOW, &tty);

	// Send data
	rc = sendData(fd, argv[1], strlen(argv[1]));
	if (rc < 0) {
		printf("Error: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

