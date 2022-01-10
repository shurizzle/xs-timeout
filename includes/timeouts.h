#ifndef __XS_TIMEOUT_CALLBACKS__
#define __XS_TIMEOUT_CALLBACKS__

#include <stddef.h>
#include <stdint.h>

typedef struct callbacks {
  uint32_t timeout;
  char **cmds;
  size_t len;
  size_t allocated;
} Callbacks;

typedef struct timeouts {
  Callbacks *callbacks;
  size_t len;
  size_t allocated;
} Timeouts;

Timeouts *timeouts_new(void);
size_t timeouts_len(Timeouts *);
void timeouts_shrink_to_fit(Timeouts *);
void timeouts_free(Timeouts *);
void timeouts_append(Timeouts *, uint32_t, char *);
void timeouts_dup_append(Timeouts *, uint32_t, char *);
Callbacks *timeouts_get(Timeouts *, uint32_t);
size_t timeouts_exec_reset(Timeouts *);
size_t timeouts_exec(Timeouts *, uint32_t, uint32_t);
uint32_t timeouts_next(Timeouts *, uint32_t);
int timeouts_inspect(Timeouts *, int (*)(void *, const char *, ...), void *);

void callbacks_shrink_to_fit(Callbacks *);
void callbacks_dup_append(Callbacks *, char *);
void callbacks_append(Callbacks *, char *);
size_t callbacks_len(Callbacks *);
size_t callbacks_exec(Callbacks *);
int callbacks_inspect(Callbacks *, int (*)(void *, const char *, ...), void *);

#endif
