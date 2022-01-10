#ifndef __XS_IDLE__
#define __XS_IDLE__

#include <X11/Xlib.h>
#include <X11/extensions/sync.h>
#include <stdint.h>
#include <time.h>

typedef enum idle_state {
  IDLE_RESET,
  IDLE_TIMEOUT,
} IdleState;

typedef struct idle {
  Display *dpy;
  int event_base;
  int error_base;
  int64_t base_timer;
  XSyncCounter idle_counter;
  IdleState idle_state;
  XSyncAlarm zero_alarm;
  XSyncAlarm timeout_alarm;
} Idle;

typedef enum select_result {
  ERROR,
  TIMEOUT,
  UNIDLE,
} SelectResult;

Idle *idle_create(void);
SelectResult idle_wait(Idle *, uint32_t);
void idle_close(Idle *);

#endif
