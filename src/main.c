#include "idle.h"
#include "options.h"
#include "timeouts.h"
#include "util.h"
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

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
    eprintf("No timeouts found.\n\n");
    eprintf(SHORT_HELP "\n");
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
    dprintf("Starting\n");
    set_handler(SIGALRM, &sigalrm_handler);
    set_handler(SIGTSTP, &sigtstp_handler);
    set_handler(SIGSTOP, &sigstop_handler);
    set_handler(SIGCONT, &sigcont_handler);
  }
  else {
    dprintf("Restarting\n");
  }

  state.last_timeout = 0;

  while (1) {
    if (state.restart) {
      state_reset();
      dprintf("RESET RESTART\n");
    }

    state_step();
    switch (idle_wait(state.idle, state.last_timeout)) {
    case ERROR:
      goto end;
    case TIMEOUT:
      state_timeout();
      dprintf("TIMEOUT\n");
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
  dprintf("RESET UNIDLE\n");
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
  dprintf("Stopping\n");

  idle_close(state.idle);
  state.idle = NULL;

  dprintf("Stopped\n");
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
  dprintf("Resuming\n");
  if (!(state.idle = idle_create())) {
    eprintf("Cannot reestabilish connection\n");
    state_destroy();
    exit(1);
  }
  state.restart = true;
  siglongjmp(startbuf, 1);
}

void sigalrm_handler(__attribute__((unused)) int sig) {
  dprintf("Restarting\n");
  idle_reset(state.idle);
  state.restart = true;
  siglongjmp(startbuf, 1);
}
