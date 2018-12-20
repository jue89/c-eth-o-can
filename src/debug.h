#ifndef DEBUG_H
#define DEBUG_H

#ifdef ENABLE_DEBUG
	#define DEBUG(...) printf(__VA_ARGS__)
#else
	#define DEBUG(...) {}
#endif

#endif
