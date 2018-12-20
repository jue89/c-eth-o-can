#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/select.h>
#include "opts.h"
#include "setup.h"
#include "tap2tty.h"

static void loop (int ttyFd, int tapFd) {
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
