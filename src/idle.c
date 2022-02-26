#include "idle.h"
#include "util.h"
#include <X11/extensions/sync.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

XSyncAlarm create_monitor_alarm(Display *, XSyncCounter *);

void i64_to_XSyncValue(int64_t n, XSyncValue *v) {
  v->hi = (int32_t)(n >> 32);
  v->lo = (uint32_t)n;
}

int64_t XSyncValue_to_i64(XSyncValue *v) {
  return (((int64_t)v->hi) << 32) | ((int64_t)v->lo);
}

XSyncAlarm create_zero_alarm(Display *, XSyncCounter *);
XSyncAlarm create_timeout_alarm(Display *, XSyncCounter *);

Idle *idle_create(void) {
  Display *dpy = NULL;
  XSyncSystemCounter *counters = NULL;
  XSyncAlarm zero_alarm = 0;
  XSyncAlarm timeout_alarm = 0;

  dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    eprintf("Cannot open display\n");
    goto err;
  }

  int major = 1, minor = 0;
  if (!XSyncInitialize(dpy, &major, &minor)) {
    eprintf("Your server doesn't support SYNC extension\n");
    goto err;
  }

  dprintf("XSync version: %d.%d\n", major, minor);

  int event_base = 0, error_base = 0;
  if (!XSyncQueryExtension(dpy, &event_base, &error_base)) {
    eprintf("Cannot query SYNC extension\n");
    goto err;
  }

  dprintf("XSync events: %d, errors: %d\n", event_base, error_base);

  int counters_len = 0;
  if (!(counters = XSyncListSystemCounters(dpy, &counters_len))) {
    eprintf("Cannot retrieve the system counters list\n");
    goto err;
  }

  XSyncCounter counter = 0;
  dprintf("Counters:\n");
  for (int i = 0; i < counters_len; ++i) {
    XSyncSystemCounter c = counters[i];

    dprintf("  %s\n", c.name);

    if (strcmp(c.name, "IDLETIME") == 0) {
      counter = c.counter;
      break;
    }
  }

  if (!counter) {
    eprintf("Cannot find IDLETIME counter\n");
    goto err;
  }

  XSyncFreeSystemCounterList(counters);
  counters = NULL;

  XSyncValue value;
  if (!XSyncQueryCounter(dpy, counter, &value)) {
    goto err;
  }

  if (XSyncValueIsNegative(value)) {
    eprintf("Counter has an invalid value.\n");
    goto err;
  }

  if (!(zero_alarm = create_zero_alarm(dpy, &counter))) {
    eprintf("Cannot create alarm\n");
    goto err;
  }

  if (!(timeout_alarm = create_timeout_alarm(dpy, &counter))) {
    eprintf("Cannot create alarm\n");
    goto err;
  }

  Idle *res = malloc(sizeof(Idle));
  res->dpy = dpy;
  res->event_base = event_base;
  res->error_base = error_base;
  res->base_timer = XSyncValue_to_i64(&value);
  res->idle_counter = counter;
  res->idle_state = IDLE_RESET;
  res->zero_alarm = zero_alarm;
  res->timeout_alarm = timeout_alarm;
  return res;
err:
  if (dpy) {
    if (counters) {
      XSyncFreeSystemCounterList(counters);
    }
    if (zero_alarm) {
      XSyncDestroyAlarm(dpy, zero_alarm);
    }
    if (timeout_alarm) {
      XSyncDestroyAlarm(dpy, timeout_alarm);
    }
    XCloseDisplay(dpy);
  }
  return NULL;
}

Status start_zero_alarm(Idle *);
Status start_timeout_alarm(Idle *, uint32_t);
Status start_timeout_alarm_i64(Idle *, int64_t);
void disable_alarms(Idle *);
void next_event(Display *, XEvent *);

void idle_reset(Idle *idle) {
  disable_alarms(idle);

  XSyncValue value;
  XSyncQueryCounter(idle->dpy, idle->idle_counter, &value);
  idle->base_timer = XSyncValue_to_i64(&value);
  idle->idle_state = IDLE_RESET;
}

#define CHECK(x)                                                               \
  if (!x) {                                                                    \
    goto err;                                                                  \
  }

SelectResult wait_reset(Idle *idle, uint32_t timeout) {
  dprintf("wait_reset(%d) with base %ld\n", timeout, idle->base_timer);
start:
  if (idle->base_timer > 1000) {
    CHECK(start_zero_alarm(idle));
  }
  CHECK(start_timeout_alarm(idle, timeout));

  while (1) {
    XEvent event;
    next_event(idle->dpy, &event);

    if (event.type == (idle->event_base + XSyncAlarmNotify)) {
      XSyncAlarmNotifyEvent *ev = (XSyncAlarmNotifyEvent *)&event;
      dprintf("Got alarm %ld (%ld, %ld)\n", ev->alarm, idle->zero_alarm,
              idle->timeout_alarm);

      if (ev->alarm == idle->zero_alarm) {
        idle->base_timer = 0;
        disable_alarms(idle);
        goto start;
      }

      if (ev->alarm == idle->timeout_alarm) {
        disable_alarms(idle);
        idle->idle_state = IDLE_TIMEOUT;
        return TIMEOUT;
      }
    }
  }
err:
  disable_alarms(idle);
  return ERROR;
}

SelectResult wait_timeout(Idle *idle, uint32_t timeout) {
  dprintf("Waiting for 0");
  CHECK(start_zero_alarm(idle));
  if (timeout) {
    dprintf(" or for timeout %u\n", timeout);
    CHECK(start_timeout_alarm(idle, timeout));
  }
  else {
    dprintf("\n");
  }

  while (1) {
    XEvent event;
    next_event(idle->dpy, &event);

    if (event.type == (idle->event_base + XSyncAlarmNotify)) {
      XSyncAlarmNotifyEvent *ev = (XSyncAlarmNotifyEvent *)&event;
      dprintf("Got alarm %ld (%ld, %ld)\n", ev->alarm, idle->zero_alarm,
              idle->timeout_alarm);

      if (ev->alarm == idle->zero_alarm) {
        idle->base_timer = 0;
        disable_alarms(idle);
        idle->idle_state = IDLE_RESET;
        return UNIDLE;
      }

      if (ev->alarm == idle->timeout_alarm) {
        disable_alarms(idle);
        return TIMEOUT;
      }
    }
  }

err:
  disable_alarms(idle);
  return ERROR;
}

#undef CHECK

SelectResult idle_wait(Idle *idle, uint32_t timeout) {
  if (idle->idle_state == IDLE_RESET) {
    return wait_reset(idle, timeout);
  } else {
    return wait_timeout(idle, timeout);
  }
}

void next_event(Display *dpy, XEvent *event) {
  int conn = ConnectionNumber(dpy);

  while (XPending(dpy) < 1) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(conn, &read_fds);
    pselect(conn + 1, &read_fds, NULL, NULL, NULL, NULL);

    XSync(dpy, 0);
  }

  XNextEvent(dpy, event);
}

Status start_zero_alarm(Idle *idle) {
  XSyncAlarmAttributes attrs = {0};
  attrs.events = 1;
  unsigned long flags = XSyncCAEvents;

  return XSyncChangeAlarm(idle->dpy, idle->zero_alarm, flags, &attrs);
}

Status start_timeout_alarm_i64(Idle *idle, int64_t timeout) {
  XSyncAlarmAttributes attrs = {0};
  timeout += idle->base_timer;
  i64_to_XSyncValue(timeout, &attrs.trigger.wait_value);
  attrs.events = 1;
  unsigned long flags = XSyncCAValue | XSyncCAEvents;

  return XSyncChangeAlarm(idle->dpy, idle->timeout_alarm, flags, &attrs);
}

Status start_timeout_alarm(Idle *idle, uint32_t timeout) {
  return start_timeout_alarm_i64(idle, ((int64_t)timeout) * 1000);
}

XSyncAlarm create_zero_alarm(Display *dpy, XSyncCounter *counter) {
  XSyncAlarmAttributes attrs = {0};

  attrs.trigger.counter = *counter;
  attrs.trigger.value_type = XSyncAbsolute;
  attrs.trigger.test_type = XSyncNegativeTransition;
  XSyncIntsToValue(&attrs.trigger.wait_value, 0, 0);
  XSyncIntsToValue(&attrs.delta, 0, 0);
  attrs.events = 0;

  unsigned long flags = XSyncCACounter | XSyncCAValueType | XSyncCAValue |
                        XSyncCATestType | XSyncCADelta | XSyncCAEvents;

  return XSyncCreateAlarm(dpy, flags, &attrs);
}

XSyncAlarm create_timeout_alarm(Display *dpy, XSyncCounter *counter) {
  XSyncAlarmAttributes attrs = {0};

  attrs.trigger.counter = *counter;
  attrs.trigger.value_type = XSyncAbsolute;
  attrs.trigger.test_type = XSyncPositiveTransition;
  XSyncIntsToValue(&attrs.trigger.wait_value, 0, 0);
  XSyncIntsToValue(&attrs.delta, 0, 0);
  attrs.events = 0;

  unsigned int flags = XSyncCACounter | XSyncCAValueType | XSyncCAValue |
                       XSyncCATestType | XSyncCADelta | XSyncCAEvents;

  return XSyncCreateAlarm(dpy, flags, &attrs);
}

Status disable_alarm(Display *dpy, XSyncAlarm alarm) {
  XSyncAlarmAttributes attrs = {0};
  attrs.events = 0;

  unsigned int flags = XSyncCAEvents;

  return XSyncChangeAlarm(dpy, alarm, flags, &attrs);
}

void disable_alarms(Idle *idle) {
  disable_alarm(idle->dpy, idle->zero_alarm);
  disable_alarm(idle->dpy, idle->timeout_alarm);
  dprintf("Started sync\n");
  XSync(idle->dpy, 1);
  dprintf("Stopped sync\n");
}

void idle_close(Idle *idle) {
  if (!idle) {
    return;
  }

  if (idle->dpy) {
    // disable_alarm(idle->dpy, idle->zero_alarm);
    // disable_alarm(idle->dpy, idle->timeout_alarm);
    disable_alarms(idle);
    eprintf("closing display\n");
    XCloseDisplay(idle->dpy);
    eprintf("display closed\n");
  }
  idle->zero_alarm = 0;
  idle->timeout_alarm = 0;
  idle->dpy = NULL;
  free(idle);
}

/*
 * switch state
 * - reset   -> create alarm to next alarm and wait for the signal, destroy the
 *               alarm
 * - timeout -> wait for signal (reset) or for the timeout (timeout)
 */
