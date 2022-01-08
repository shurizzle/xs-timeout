#ifndef __XS_IDLE__
#define __XS_IDLE__

#include <X11/Xlib.h>
#include <X11/extensions/sync.h>
#include <time.h>

typedef struct idle {
  Display *dpy;
  XSyncAlarm alarm;
  int event_base;
  int error_base;
} Idle;

typedef enum select_result {
  ERROR,
  TIMEOUT,
  UNIDLE,
} SelectResult;

Idle *idle_create(void);
SelectResult idle_select(Idle *, struct timespec *);
void idle_close(Idle *);

#endif
