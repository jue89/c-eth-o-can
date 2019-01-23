#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "opts.h"
#include "setup.h"
#include "mode-emulated.h"
#include "mode-native.h"

int main (int argc, char **argv) {
	struct opts options;
	int ttyFd, tapFd, senseFd;

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

	// Open sense GPIO
	senseFd = allocSenseGpio(options.sensePath);
	if (senseFd < 0) {
		perror("open sense GPIO");
		return EXIT_FAILURE;
	}

	if (strcmp(options.mode, "emulated") == 0) {
		loopEmulated(ttyFd, tapFd, senseFd);
	} else if (strcmp(options.mode, "native") == 0) {
		loopNative(ttyFd, tapFd, senseFd);
	} else {
		fprintf(stderr, "Unknow mode: %s\n", options.mode);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
