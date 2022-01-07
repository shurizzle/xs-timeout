#include <X11/Xlib.h>
#include <X11/extensions/sync.h>
#include <X11/extensions/syncconst.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include "timeouts.h"
#include "options.h"

size_t oparse_timeouts(const char **timeouts, size_t offset, size_t len,
                      time_t **);
XSyncAlarm create_monitor_alarm(Display *dpy, XSyncCounter *counter);
bool is_fd_valid(int fd);
void timespec_diff(struct timespec *result, struct timespec *start,
                   struct timespec *stop);

int main(int argc, char **argv) {
  int code = 0;
  time_t *timeouts = NULL;
  Display *dpy = NULL;
  XSyncSystemCounter *counters = NULL;
  XSyncAlarm alarm = 0;

  Options opts = parse_options(argc, argv);

  if (opts.help) {
    printf("HELPS HERE\n");
    goto end;
  }

  if (!opts.timeouts) {
    fprintf(stderr, "ERROR: HELPS\n");
    code = 1;
    goto end;
  }

  timeouts_inspect(opts.timeouts, (int (*)(void *, const char *, ...)) fprintf, stderr);
  fputs("\n", stderr);

  timeouts_free(opts.timeouts);

  size_t timeouts_len =
      oparse_timeouts((const char **)argv, 1, argc - 1, &timeouts);
#ifdef DEBUG
  fprintf(stderr, "Timeouts: [");
  for (size_t i = 0; i < timeouts_len; ++i) {
    if (i != 0)
      fputs(", ", stderr);
    fprintf(stderr, "%ld", timeouts[i]);
  }
  fputs("]\n", stderr);
#endif

  if (!timeouts && errno == ERANGE) {
    fprintf(stderr, "'%s` is not a valid timeout\n", argv[timeouts_len + 1]);
    code = 1;
    goto end;
    exit(1);
  } else if (timeouts_len == 0) {
    fprintf(stderr, "You have to specify at least 1 timeout\n");
    code = 1;
    goto end;
  }

  dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    fprintf(stderr, "Cannot open display\n");
    code = 1;
    goto end;
  }

  int major = 1, minor = 0;
  if (!XSyncInitialize(dpy, &major, &minor)) {
    fprintf(stderr, "Your server doesn't support Sync extension\n");
    code = 1;
    goto end;
  }

#ifdef DEBUG
  fprintf(stderr, "XSync version: %d.%d\n", major, minor);
#endif

  int event_base = 0, error_base = 0;
  if (!XSyncQueryExtension(dpy, &event_base, &error_base)) {
    fprintf(stderr, "Cannot query Sync extension\n");
    code = 1;
    goto end;
  }

#ifdef DEBUG
  fprintf(stderr, "XSync events: %d, errors: %d\n", event_base, error_base);
#endif

  int counters_len = 0;
  if (!(counters = XSyncListSystemCounters(dpy, &counters_len))) {
    fprintf(stderr, "Cannot retrieve the system counters list\n");
    code = 1;
    goto end;
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
  code = 1;
  goto end;

have_counter:
  XSyncFreeSystemCounterList(counters);
  counters = NULL;
#ifdef DEBUG
  fprintf(stderr, "Creating alarm\n");
#endif

  alarm = create_monitor_alarm(dpy, &counter);
  if (!alarm) {
    fprintf(stderr, "Cannot create timer\n");
    code = 1;
    goto end;
  }
#ifdef DEBUG
  fprintf(stderr, "Starting loop\n");
#endif

  int conn = ConnectionNumber(dpy);
#ifdef DEBUG
  fprintf(stderr, "connection = %d\n", conn);
#endif
  fd_set read_fds;
  sigset_t ss;

  struct state {
    struct timespec last_unidle;
    size_t next_run;
  } state = {0};

  clock_gettime(CLOCK_MONOTONIC, &state.last_unidle);
  state.next_run = 0;

  do {
    if (XPending(dpy) < 1) {
      int flags = fcntl(conn, F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl(conn, F_SETFL, flags);

      FD_ZERO(&read_fds);
      FD_SET(conn, &read_fds);

      sigemptyset(&ss);
      sigaddset(&ss, SIGSTOP);
      struct timespec inner_duration;
      struct timespec *duration = &inner_duration;

      if (state.next_run < timeouts_len) {
        struct timespec tmp;
        struct timespec tmp2 = {0};
        clock_gettime(CLOCK_MONOTONIC, duration);
        timespec_diff(&tmp, duration, &state.last_unidle);
        tmp2.tv_sec = timeouts[state.next_run];
        timespec_diff(duration, &tmp2, &tmp);
      } else {
        duration = NULL;
      }

#ifdef DEBUG
      fprintf(stderr, "pselect()");
      fflush(stderr);
#endif

      int select_res = pselect(conn + 1, &read_fds, NULL, NULL, duration, &ss);

#ifdef DEBUG
      fprintf(stderr, " = %d\n", select_res);
#endif

      if (select_res < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        code = 1;
        goto end;
      }

      if (!select_res) {
        struct timespec result;
        clock_gettime(CLOCK_MONOTONIC, duration);
        timespec_diff(&result, duration, &state.last_unidle);
        time_t timeout = result.tv_sec;
#ifdef DEBUG
        fprintf(stderr, "timeout %ld\n", timeout);
#endif

        for (size_t i = state.next_run;
             i < timeouts_len && timeouts[i] <= timeout; ++i) {
          state.next_run = i + 1;
          printf("%ld\n", timeouts[i]);
        }

        continue;
      }

      if (!is_fd_valid(conn)) {
        fprintf(stderr, "Connection to server interrupted\n");
        code = 1;
        goto end;
      }
    }

    XEvent event;
    int res = XNextEvent(dpy, &event);
    if (res) {
      char buf[150];
      XGetErrorText(dpy, res, buf, 150);
      fprintf(stderr, "Cannot get the next event: %s\n", buf);
      code = 1;
      goto end;
    }

    if (event.type != (event_base + XSyncAlarmNotify)) {
      continue;
    }

    if (state.next_run != 0) {
      state.next_run = 0;
      printf("reset\n");
    }

    clock_gettime(CLOCK_MONOTONIC, &state.last_unidle);
    state.next_run = 0;
  } while (1);

end:
  if (alarm) {
    XSyncDestroyAlarm(dpy, alarm);
  }
  if (counters) {
    XSyncFreeSystemCounterList(counters);
  }
  if (dpy) {
    XCloseDisplay(dpy);
  }
  if (timeouts) {
    free(timeouts);
  }
  return code;
}

#define TIME_MAX (((time_t)1 << (sizeof(time_t) * CHAR_BIT - 2)) - 1) * 2 + 1

time_t oparse_timeout(const char *timeout, int base) {
  time_t res = TIME_MAX;
  unsigned long long tmp;
  char *endptr = NULL;

  tmp = strtoull(timeout, &endptr, base);
  if (tmp == ULLONG_MAX && errno == ERANGE) {
    return res;
  }

  timeout += strlen(timeout) * sizeof(char);
  while (*endptr && isspace(*endptr)) {
    endptr++;
  }

  if (timeout != endptr || tmp > (unsigned long long)res) {
    errno = ERANGE;
  } else {
    res = (time_t)tmp;
  }

  return res;
}

int sort_cmp(const void *_a, const void *_b) {
  size_t a = *((size_t *)_a);
  size_t b = *((size_t *)_b);

  return a - b;
}

size_t oparse_timeouts(const char **timeouts, size_t offset, size_t len,
                      time_t **result) {
  if (len < 1) {
    *result = NULL;
    return 0;
  }

  *result = malloc(len * sizeof(time_t));

  size_t i;
  for (i = 0; i < len; ++i) {
    time_t tmp = oparse_timeout(timeouts[i + offset], 10);

    if (tmp == TIME_MAX && errno == ERANGE) {
      goto err;
    } else if (tmp == 0) {
      goto err;
    }

    (*result)[i] = tmp;
  }

  qsort(*result, i, sizeof(time_t), sort_cmp);

  return i;
err:
  if (*result) {
    free(*result);
  }
  *result = NULL;
  return i;
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

bool is_fd_valid(int fd) { return fcntl(fd, F_GETFD) != -1 || errno != EBADF; }

int timespec_cmp(struct timespec *a, struct timespec *b) {
  int res = a->tv_sec - b->tv_sec;
  if (!res) {
    return res;
  }
  return a->tv_nsec - b->tv_nsec;
}

void timespec_diff(struct timespec *result, struct timespec *a,
                   struct timespec *b) {
  if ((a->tv_nsec - b->tv_nsec) < 0) {
    result->tv_sec = a->tv_sec - b->tv_sec - 1;
    result->tv_nsec = a->tv_nsec - b->tv_nsec + 1000000000;
  } else {
    result->tv_sec = a->tv_sec - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
  }

  return;
}
