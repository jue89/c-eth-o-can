#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "mode-native.h"
#include "config.h"

struct packet {
	uint16_t len;
	char data[MTU];
};

static int readBytes(int fd, void *buf, int expected) {
	int rc;
	int received = 0;

	while (expected) {
		fd_set rfds;
		struct timeval timeout;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;
		rc = select(fd + 1, &rfds, NULL, NULL, &timeout);
		if (rc < 1) return -1;

		rc = read(fd, buf + received, expected);
		if (rc <= 0) return -1;
		received += rc;
		expected -= rc;
	}

	return received;
}

static void net2host(int ttyFd, int tapFd) {
	struct packet pkt;

	// Read length field
	if (readBytes(ttyFd, &pkt, 2) != 2) return;

	// Read MAC frame
	uint16_t len = ntohs(pkt.len);
	if (len > MTU) return;
	if (readBytes(ttyFd, pkt.data, len) != len) return;

	// Write frame to TAP
	if (write(tapFd, pkt.data, len) < 0) perror("write to tap device");
}

static void host2net(int ttyFd, int tapFd, int senseFd) {
	struct packet pkt;
	ssize_t n;

	// Read packet from TAP device
	n = read(tapFd, pkt.data, MTU);
	if (n < 0) {
		perror("read from tty device");
		return;
	}

	// Set length byte
	pkt.len = htons((uint16_t) n);

	// Wait for RDY pin to become HIGH
	while (1) {
		int rc;
		char strValue[2];
		fd_set rfds;
		struct timeval timeout;

		// Get current state
		lseek(senseFd, 0, SEEK_SET);
		rc = read(senseFd, strValue, sizeof(strValue));
		if (rc < 1) return;

		// RDY line is high -> go ahead!
		if (strValue[0] == '0') break;

		// Wait for changing line
		FD_ZERO(&rfds);
		FD_SET(senseFd, &rfds);
		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;
		rc = select(senseFd + 1, NULL, NULL, &rfds, &timeout);
		if (rc < 1) return;
	}

	// Send frame
	write(ttyFd, &pkt, n + 2);
}

extern void loopNative (int ttyFd, int tapFd, int senseFd) {
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
			net2host(ttyFd, tapFd);
		} else if (FD_ISSET(tapFd, &rfds)) {
			// Data from TAP device
			host2net(ttyFd, tapFd, senseFd);
		}
	}
}
