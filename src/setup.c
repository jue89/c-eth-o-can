#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include "setup.h"

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

static int setGpioOption (const char *base, const char *opt, const char *val) {
	int fd;
	char path[256];

	snprintf(path, sizeof(path), "%s/%s", base, opt);
	fd = open(path, O_RDWR);
	if (fd < 0) return fd;
	write(fd, val, strlen(val));
	close(fd);
	return 0;
}

int allocSenseGpio (const char *base) {
	char path[256];

	// Set direction and interrupt edge
	if (setGpioOption(base, "direction", "in")) return -1;
	if (setGpioOption(base, "edge", "both")) return -1;

	// Finally open value file
	snprintf(path, sizeof(path), "%s/value", base);
	return open(path, O_RDONLY);
}
