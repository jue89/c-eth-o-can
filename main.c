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

#define ENABLE_DEBUG
#ifdef ENABLE_DEBUG
	#define DEBUG(f_, ...) printf((f_), __VA_ARGS__)
#else
	#define DEBUG(f_, ...) {}
#endif

static void flushPendingData (int fd) {
	int rc;
	char c;
	do {
		rc = read(fd, &c, 1);
		if (rc) DEBUG("--> Flush 0x%02x\n", c);
	} while (rc > 0);
}

static int sendOctet (int fd, char octet) {
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

	// Wait up to 1ms for echo
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

static ssize_t sendData (int fd, char * buf, size_t count) {
	int i, rc;
	ssize_t sentBytes = 0;

	// Make sure no old data is left in read queue
	flushPendingData(fd);

	// Send each data byte
	for (i = 0; i < count; i++) {
		rc = sendOctet(fd, buf[i]);
		// Some error occured ... errno is set correctly
		if (rc < 0) return -1;
		sentBytes++;
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

