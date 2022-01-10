#include "idle.h"
#include "options.h"
#include "timeouts.h"
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct state {
  uint32_t prev_timeout;
  uint32_t last_timeout;
  Timeouts *timeouts;
  Idle *idle;
  bool restart;
} state = {
    0, 0, NULL, NULL, false,
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
void state_step();

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
  }
#ifdef DEBUG
  else {
    fprintf(stderr, "Restarting\n");
  }
#endif

  state.last_timeout = 0;

  while (1) {
    if (state.restart) {
      state_reset();
#ifdef DEBUG
      fprintf(stderr, "RESET RESTART\n");
#endif
    }

    state_step();
    switch (idle_wait(state.idle, state.last_timeout)) {
    case ERROR:
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

void state_timeout() {
  if (state.last_timeout != 0) {
    timeouts_exec(state.timeouts, state.prev_timeout, state.last_timeout);
  }
}

void state_reset() {
  timeouts_exec_reset(state.timeouts);
#ifdef DEBUG
  fprintf(stderr, "RESET UNIDLE\n");
#endif
  state.prev_timeout = 0;
  state.last_timeout = 0;
  state.restart = false;
}

void state_step() {
  state.prev_timeout = state.last_timeout;
  state.last_timeout = timeouts_next(state.timeouts, state.last_timeout);
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
  sa.sa_flags = SA_ONSTACK | SA_NODEFER;
  sa.sa_handler = handler;
  sigaction(signal, &sa, NULL);
}

void sigstop_handler(__attribute__((unused)) int sig) {
#ifdef DEBUG
  fprintf(stderr, "Stopping\n");
#endif

  idle_close(state.idle);
  state.idle = NULL;

#ifdef DEBUG
  fprintf(stderr, "Stopped\n");
#endif
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
