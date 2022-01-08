#include "idle.h"
#include <X11/extensions/sync.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

XSyncAlarm create_monitor_alarm(Display *, XSyncCounter *);

Idle *idle_create(void) {
  Display *dpy = NULL;
  XSyncSystemCounter *counters = NULL;
  XSyncAlarm alarm = 0;

  dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    fprintf(stderr, "Cannot open display\n");
    goto err;
  }

  int major = 1, minor = 0;
  if (!XSyncInitialize(dpy, &major, &minor)) {
    fprintf(stderr, "Your server doesn't support SYNC extension\n");
    goto err;
  }

#ifdef DEBUG
  fprintf(stderr, "XSync version: %d.%d\n", major, minor);
#endif

  int event_base = 0, error_base = 0;
  if (!XSyncQueryExtension(dpy, &event_base, &error_base)) {
    fprintf(stderr, "Cannot query SYNC extension\n");
    goto err;
  }

#ifdef DEBUG
  fprintf(stderr, "XSync events: %d, errors: %d\n", event_base, error_base);
#endif

  int counters_len = 0;
  if (!(counters = XSyncListSystemCounters(dpy, &counters_len))) {
    fprintf(stderr, "Cannot retrieve the system counters list\n");
    goto err;
  }

  XSyncCounter counter = 0;
#ifdef DEBUG
  fprintf(stderr, "Counters:\n");
#endif
  for (int i = 0; i < counters_len; ++i) {
    XSyncSystemCounter c = counters[i];

#ifdef DEBUG
    fprintf(stderr, "  %s\n", c.name);
#endif

    if (strcmp(c.name, "IDLETIME") == 0) {
      counter = c.counter;
      goto have_counter;
    }
  }

  fprintf(stderr, "Cannot find IDLETIME counter\n");
  goto err;

have_counter:
  XSyncFreeSystemCounterList(counters);
  counters = NULL;

  alarm = create_monitor_alarm(dpy, &counter);
  if (!alarm) {
    fprintf(stderr, "Cannot create SYNC alarm\n");
    goto err;
  }

  Idle *res = malloc(sizeof(Idle));
  res->dpy = dpy;
  res->alarm = alarm;
  res->event_base = event_base;
  res->error_base = error_base;
  return res;
err:
  if (alarm) {
    XSyncDestroyAlarm(dpy, alarm);
  }
  if (counters) {
    XSyncFreeSystemCounterList(counters);
  }
  if (dpy) {
    XCloseDisplay(dpy);
  }
  return NULL;
}

SelectResult idle_select(Idle *idle, struct timespec *timeout) {
  while (1) {
    if (XPending(idle->dpy) < 1) {
      int conn = ConnectionNumber(idle->dpy);
      int flags = fcntl(conn, F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl(conn, F_SETFL, flags);

      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(conn, &read_fds);

#ifdef DEBUG
      fprintf(stderr, "pselect(");
      if (timeout) {
        fprintf(stderr, "tv_sec: %ld, tv_nsec: %ld", timeout->tv_sec,
                timeout->tv_nsec);
      } else {
        fprintf(stderr, "null");
      }
      fprintf(stderr, ")");
      fflush(stderr);
#endif

      int select_res = pselect(conn + 1, &read_fds, NULL, NULL, timeout, NULL);

#ifdef DEBUG
      fprintf(stderr, " = %d\n", select_res);
#endif

      if (select_res < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        return ERROR;
      }

      if (!select_res) {
        return TIMEOUT;
      }
    }

    XEvent event;
    XNextEvent(idle->dpy, &event); /* No error handling? */

    if (event.type != (idle->event_base + XSyncAlarmNotify)) {
      continue;
    }

    return UNIDLE;
  }
}

void idle_close(Idle *idle) {
  if (!idle) {
    return;
  }
  if (idle->dpy) {
    if (idle->alarm) {
      XSyncDestroyAlarm(idle->dpy, idle->alarm);
    }

    XCloseDisplay(idle->dpy);
  }
  idle->alarm = 0;
  idle->dpy = NULL;
  free(idle);
}

XSyncAlarm create_monitor_alarm(Display *dpy, XSyncCounter *counter) {
  XSyncAlarmAttributes attrs = {0};

  attrs.trigger.counter = *counter;
  attrs.trigger.value_type = XSyncAbsolute;
  attrs.trigger.test_type = XSyncNegativeTransition;
  XSyncIntsToValue(&attrs.trigger.wait_value, 0, 0);
  XSyncIntsToValue(&attrs.delta, 0, 0);
  attrs.events = 1;

  unsigned int flags = XSyncCACounter | XSyncCAValueType | XSyncCAValue |
                       XSyncCATestType | XSyncCADelta | XSyncCAEvents;

  return XSyncCreateAlarm(dpy, flags, &attrs);
}
