#ifndef _XSS_TIMEOUT_CALLBACKS
#define _XSS_TIMEOUT_CALLBACKS

#include <stdlib.h>

typedef struct callbacks {
  time_t timeout;
  char **cmds;
  size_t cmds_len;
} callbacks;

#endif
