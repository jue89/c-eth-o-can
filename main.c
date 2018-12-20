#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>

#define ENABLE_DEBUG
#ifdef ENABLE_DEBUG
	#define DEBUG(...) printf(__VA_ARGS__)
#else
	#define DEBUG(...) {}
#endif

#define C_END 0xff
#define C_ESC 0xfe

static uint16_t crc16Update (uint16_t crc, char octet) {
	crc = (uint8_t)(crc >> 8) | (crc << 8);
	crc ^= octet;
	crc ^= (uint8_t)(crc & 0xff) >> 4;
	crc ^= (crc << 8) << 4;
	crc ^= ((crc & 0xff) << 4) << 1;
	return crc;
}

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

void usage (const char *name) {
	fprintf(stderr, "Usage: %s [opts ...]\n", name);
	fprintf(stderr, "opts:\n");
	fprintf(stderr, "     -d [ttyDevice] --tty-device=[ttyDevice]\n");
	fprintf(stderr, "           Set the TTY device connected to CAN transceiver\n");
}

struct opts {
	char * ttyDevice;
};

int parseOpts (struct opts *o, int argc, char **argv) {
	static const char *defOpts = "d:";
	static struct option defLongOpts[] = {
		{"tty-device", required_argument, NULL, 'd'},
		{0, 0, 0, 0}
	};

	// Set default
	o->ttyDevice = NULL;

	// Parse opts
	while (1) {
		int c = getopt_long(argc, argv, defOpts, defLongOpts, NULL);
		if (c == -1) break;
		switch (c) {
		case 'd':
			o->ttyDevice = optarg;
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	// Enforece required opts
	if (!o->ttyDevice) {
		fprintf(stderr, "ttyDevice must be specified!\n");
		usage(argv[0]);
		return -1;
	}

	return 0;
}

int main (int argc, char **argv) {
	int rc;
	struct opts options;
	int fd;
	struct termios tty;

	// Read cmd line options
	if (parseOpts(&options, argc, argv)) return EXIT_FAILURE;

	// Setup tty device
	fd = open(options.ttyDevice, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		printf("Cannot open TTY device: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}
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

