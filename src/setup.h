#ifndef SETUP_H
#define SETUP_H

extern int allocTty (char *dev);
extern int allocTap (char *dev);
extern int allocSenseGpio (const char *base);

#endif
