#include "idle.h"
#include "options.h"
#include "timeouts.h"
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct state {
  struct timespec last_unidle;
  time_t last_timeout;
  Timeouts *timeouts;
  Idle *idle;
  bool restart;
} state = {
    {0, 0}, 0, NULL, NULL, false,
};

sigjmp_buf startbuf;

void set_handler(int, void (*)(int));
void sigalrm_handler(int);
void sigcont_handler(int);
void sigtstp_handler(int);
void sigstop_handler(int);
void state_destroy();
void state_reset();
void state_timeout();
struct timespec *next_timeout(struct timespec *timeout);

#define VERSION "0.0.1"

#define SHORT_HELP                                                             \
  "xs-timeout [-h|-v|[<seconds>:<command>]+ [reset:<command>]*]"

#define HELP                                                                   \
  "xs-timeout v" VERSION "\n"                                                  \
  "\n"                                                                         \
  "USAGE: " SHORT_HELP

int main(int argc, char **argv) {
  int code = 0;
  Options opts = parse_options(argc, argv);

  if (opts.help) {
    printf(HELP "\n");
    goto end;
  }

  if (opts.version) {
    printf(VERSION "\n");
    goto end;
  }

  if (!opts.timeouts) {
    fprintf(stderr, "No timeouts found.\n\n");
    fprintf(stderr, SHORT_HELP "\n");
    code = 1;
    goto end;
  }

#ifdef DEBUG
  timeouts_inspect(opts.timeouts, (int (*)(void *, const char *, ...))fprintf,
                   stderr);
  fputs("\n", stderr);
#endif

  state.idle = idle_create();
  if (!state.idle) {
    /* Errors already printed */
    code = 1;
    goto end;
  }

  clock_gettime(CLOCK_MONOTONIC, &state.last_unidle);
  state.timeouts = opts.timeouts;
  opts.timeouts = NULL;

  if (sigsetjmp(startbuf, 1) == 0) {
#ifdef DEBUG
    fprintf(stderr, "Starting\n");
#endif
    set_handler(SIGALRM, &sigalrm_handler);
    set_handler(SIGTSTP, &sigtstp_handler);
    set_handler(SIGSTOP, &sigstop_handler);
    set_handler(SIGCONT, &sigcont_handler);
  } else {
#ifdef DEBUG
    fprintf(stderr, "Restarting\n");
#endif
  }

  struct timespec timeout;

  while (1) {
    if (state.restart) {
      state_reset();
#ifdef DEBUG
      fprintf(stderr, "RESET RESTART\n");
#endif
    }

    switch (idle_select(state.idle, next_timeout(&timeout))) {
    case ERROR:
      fprintf(stderr, "ERROR: %s\n", strerror(errno));
      goto end;
    case TIMEOUT:
      state_timeout();
#ifdef DEBUG
      fprintf(stderr, "TIMEOUT\n");
#endif
      break;
    case UNIDLE:
      state_reset();
      break;
    }
  }

end:
  if (opts.timeouts) {
    timeouts_free(opts.timeouts);
  }
  state_destroy();
  return code;
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

struct timespec elapsed() {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  struct timespec _elapsed;
  timespec_diff(&_elapsed, &now, &state.last_unidle);
  return _elapsed;
}

size_t _state_timeout() {
  size_t res = 0;
  time_t next = elapsed().tv_sec;
  if (next != state.last_timeout) {
    res = timeouts_exec(state.timeouts, state.last_timeout, next);
    state.last_timeout = next;
  }
  return res;
}

void state_timeout() {
  while (_state_timeout())
    ;
}

void state_reset() {
  if (state.last_timeout != 0) {
    timeouts_exec_reset(state.timeouts);
#ifdef DEBUG
    fprintf(stderr, "RESET UNIDLE\n");
#endif
  }
  clock_gettime(CLOCK_MONOTONIC, &state.last_unidle);
  state.last_timeout = 0;
  state.restart = false;
}

struct timespec *next_timeout(struct timespec *timeout) {
  // let elapsed = now - last
  // let n = next(elapsed)
  // return NULL if !n
  // n - elapsed
  struct timespec _elapsed = elapsed();

#ifdef DEBUG
  fprintf(stderr, "elapsed = %ld.%ld\n", _elapsed.tv_sec, _elapsed.tv_nsec);
#endif

  struct timespec next;
  memcpy(&next, &_elapsed, sizeof(struct timespec));
  if (!timeouts_next(state.timeouts, &next)) {
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "next = %ld.%ld\n", next.tv_sec, next.tv_nsec);
#endif

  timespec_diff(timeout, &next, &_elapsed);

#ifdef DEBUG
  fprintf(stderr, "timeout = %ld.%ld\n", timeout->tv_sec, timeout->tv_nsec);
#endif
  return timeout;
}

void state_destroy() {
  if (state.idle) {
    idle_close(state.idle);
  }
  if (state.timeouts) {
    timeouts_free(state.timeouts);
  }
}

void set_handler(int signal, void (*handler)(int)) {
  struct sigaction sa;
  sa.sa_flags = SA_RESTART | SA_ONSTACK;
  sa.sa_handler = handler;
  sigaction(signal, &sa, NULL);
}

void sigstop_handler(__attribute__((unused)) int sig) {
#ifdef DEBUG
  fprintf(stderr, "Stopping\n");
#endif
  idle_close(state.idle);
  state.idle = NULL;
}

void sigtstp_handler(__attribute__((unused)) int sig) {
  sigset_t tstp_mask, prev_mask;
  int savedErrno = errno;

  sigstop_handler(SIGSTOP);

  signal(SIGTSTP, SIG_DFL);

  raise(SIGTSTP);

  sigemptyset(&tstp_mask);
  sigaddset(&tstp_mask, SIGTSTP);
  sigprocmask(SIG_UNBLOCK, &tstp_mask, &prev_mask);

  sigprocmask(SIG_SETMASK, &prev_mask, NULL);

  set_handler(SIGTSTP, &sigtstp_handler);

  errno = savedErrno;
}

void sigcont_handler(__attribute__((unused)) int sig) {
#ifdef DEBUG
  fprintf(stderr, "Resuming\n");
#endif
  if (!(state.idle = idle_create())) {
    fprintf(stderr, "Cannot reestabilish connection\n");
    state_destroy();
    exit(1);
  }
  state.restart = true;
  siglongjmp(startbuf, 1);
}

void sigalrm_handler(__attribute__((unused)) int sig) {
#ifdef DEBUG
  fprintf(stderr, "Restarting\n");
#endif
  state.restart = true;
  siglongjmp(startbuf, 1);
}
