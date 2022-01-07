#ifndef __XS_TIMEOUT_CALLBACKS__
#define __XS_TIMEOUT_CALLBACKS__

#include <stddef.h>
#include <time.h>

typedef struct callbacks {
  time_t timeout;
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
void timeouts_append(Timeouts *, time_t, char *);
void timeouts_dup_append(Timeouts *, time_t, char *);
Callbacks *timeouts_get(Timeouts *timeouts, time_t time);
void timeouts_exec_reset(Timeouts *timeouts);
int timeouts_inspect(Timeouts *, int (*)(void *, const char *, ...), void *);

void callbacks_shrink_to_fit(Callbacks *);
void callbacks_dup_append(Callbacks *, char *);
void callbacks_append(Callbacks *, char *);
size_t callbacks_len(Callbacks *);
int callbacks_inspect(Callbacks *, int (*)(void *, const char *, ...), void *);

#endif
