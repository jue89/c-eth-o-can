#ifndef OPTS_H
#define OPTS_h

struct opts {
	char * ttyDevice;
	char * tapDevice;
};

extern int parseOpts (struct opts *o, int argc, char **argv);

#endif
