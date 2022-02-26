#ifndef __XS_TIMEOUT_UTIL__
#define __XS_TIMEOUT_UTIL__

#include <stdio.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

#ifdef DEBUG
#define dprintf(...) eprintf(__VA_ARGS__)
#else
#define dprintf(...)
#endif

#endif
