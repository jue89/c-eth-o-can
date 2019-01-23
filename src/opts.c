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
	fprintf(stderr, " -s [senseGpioPath] --sense-gpio=[senseGpioPath]\n");
	fprintf(stderr, "       Path to the exported GPIO (e.g. /sys/class/gpio/gpio27)\n");
}

int parseOpts (struct opts *o, int argc, char **argv) {
	static const char *defOpts = "m:d:n:s:";
	static struct option defLongOpts[] = {
		{"mode", required_argument, NULL, 'm'},
		{"tty-device", required_argument, NULL, 'd'},
		{"tap-device", required_argument, NULL, 'n'},
		{"sense-gpio", required_argument, NULL, 's'},
		{0, 0, 0, 0}
	};

	// Set default
	o->mode = "native";
	o->ttyDevice = NULL;
	o->tapDevice = NULL;
	o->sensePath = NULL;

	// Parse opts
	while (1) {
		int c = getopt_long(argc, argv, defOpts, defLongOpts, NULL);
		if (c == -1) break;
		switch (c) {
		case 'm':
			o->mode = optarg;
			break;
		case 'd':
			o->ttyDevice = optarg;
			break;
		case 'n':
			o->tapDevice = optarg;
			break;
		case 's':
			o->sensePath = optarg;
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
	if (!o->sensePath) {
		fprintf(stderr, "senseGpioPath must be specified!\n");
		usage(argv[0]);
		return -1;
	}

	return 0;
}
