#include <stdio.h>
#include <getopt.h>
#include "opts.h"

static void usage (const char *name) {
	fprintf(stderr, "Usage: %s [opts ...]\n", name);
	fprintf(stderr, "opts:\n");
	fprintf(stderr, " -d [ttyDevice] --tty-device=[ttyDevice]\n");
	fprintf(stderr, "       Set the TTY device connected to CAN transceiver\n");
	fprintf(stderr, " -n [tapDevice] --tap-device=[tapDevice]\n");
	fprintf(stderr, "       TAP device to connect to\n");
}

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
