#ifndef __XS_OPTIONS__
#define __XS_OPTIONS__

#include "timeouts.h"
#include <stdbool.h>

typedef struct options {
  bool help;
  Timeouts *timeouts;
} Options;

Options parse_options(int argc, char **argv);

#endif
