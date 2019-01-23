#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "opts.h"
#include "setup.h"
#include "mode-emulated.h"

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

	loopEmulated(ttyFd, tapFd);

	return EXIT_SUCCESS;
}
