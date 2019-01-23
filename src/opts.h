#ifndef OPTS_H
#define OPTS_h

struct opts {
	char * mode;
	char * ttyDevice;
	char * tapDevice;
	char * sensePath;
};

extern int parseOpts (struct opts *o, int argc, char **argv);

#endif
