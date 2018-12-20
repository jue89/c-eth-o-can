#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
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
#include <linux/if.h>
#include <linux/if_tun.h>
#include <time.h>

//#define ENABLE_DEBUG
#ifdef ENABLE_DEBUG
	#define DEBUG(...) printf(__VA_ARGS__)
#else
	#define DEBUG(...) {}
#endif

#define MTU 2048
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

void tap2tty (int tapFd, int ttyFd) {
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
		printf("TAP > TTY: %d Bytes ...", n);
		if (tries-- > 0) {
			int delay = rand() & 0xfff;
			delay += 5000; // 5000..9095us
			printf(" failed. Retry in %d us.\n", delay);
			usleep(delay);
			goto retry;
		} else {
			printf(" failed. Drop.\n");
		}
	} else {
		printf("TAP > TTY: %d Bytes ... sent.\n", n);
	}
}

void loop (int ttyFd, int tapFd) {
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
	FD_ZERO(&rfds);
	FD_SET(tapFd, &rfds);
	rc = select(maxFd, &rfds, NULL, NULL, NULL);
	if (rc <= 0) {
		return;
	}

	// Data from TAP device
	if (FD_ISSET(tapFd, &rfds)) {
		tap2tty(tapFd, ttyFd);
	}

}

int allocTty (char *dev) {
	int fd;
	struct termios tty;

	fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) return fd;

	memset(&tty, 0, sizeof(tty));
	tcgetattr(fd, &tty);
	cfmakeraw(&tty);
	tty.c_cc[VMIN]  = 1; // One input byte is enough to return from read()
	tty.c_cc[VTIME] = 0;
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);
	tcsetattr(fd, TCSANOW, &tty);

	return fd;
}

int allocTap (char *dev) {
	struct ifreq ifr;
	int fd, rc;

	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) return fd;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	rc = ioctl(fd, TUNSETIFF, (void *)&ifr);
	if (rc < 0) {
		close(fd);
		return rc;
	}

	return fd;
}

void usage (const char *name) {
	fprintf(stderr, "Usage: %s [opts ...]\n", name);
	fprintf(stderr, "opts:\n");
	fprintf(stderr, " -d [ttyDevice] --tty-device=[ttyDevice]\n");
	fprintf(stderr, "       Set the TTY device connected to CAN transceiver\n");
	fprintf(stderr, " -n [tapDevice] --tap-device=[tapDevice]\n");
	fprintf(stderr, "       TAP device to connect to\n");
}

struct opts {
	char * ttyDevice;
	char * tapDevice;
};

int parseOpts (struct opts *o, int argc, char **argv) {
	static const char *defOpts = "d:n:";
	static struct option defLongOpts[] = {
		{"tty-device", required_argument, NULL, 'd'},
		{"tap-device", required_argument, NULL, 'n'},
		{0, 0, 0, 0}
	};

	// Set default
	o->ttyDevice = NULL;
	o->tapDevice = NULL;

	// Parse opts
	while (1) {
		int c = getopt_long(argc, argv, defOpts, defLongOpts, NULL);
		if (c == -1) break;
		switch (c) {
		case 'd':
			o->ttyDevice = optarg;
			break;
		case 'n':
			o->tapDevice = optarg;
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
	if (!o->tapDevice) {
		fprintf(stderr, "tapDevice must be specified!\n");
		usage(argv[0]);
		return -1;
	}

	return 0;
}

int main (int argc, char **argv) {
	struct opts options;
	int ttyFd, tapFd;

	srand(time(NULL));

	// Read cmd line options
	if (parseOpts(&options, argc, argv)) return EXIT_FAILURE;

	// Open tty device
	ttyFd = allocTty(options.ttyDevice);
	if (ttyFd < 0) {
		perror("open TTY device");
		return EXIT_FAILURE;
	}

	// Open TAP device
	tapFd = allocTap(options.tapDevice);
	if (tapFd < 0) {
		perror("open TAP device");
		return EXIT_FAILURE;
	}

	while (1) {
		loop(ttyFd, tapFd);
	}

	return EXIT_SUCCESS;
}

