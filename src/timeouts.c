#include "timeouts.h"
#include "daemon.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

inline size_t callbacks_len(Callbacks *callbacks) {
  return callbacks->len;
}

void callbacks_shrink_to_fit(Callbacks *callbacks) {
  if (!callbacks->len) {
    if (callbacks->cmds) {
      free(callbacks->cmds);
      callbacks->cmds = NULL;
    }
  } else {
    callbacks->cmds = realloc(callbacks->cmds, callbacks->len * sizeof(char **));
  }

  callbacks->allocated = callbacks->len;
}

void _callbacks_free(Callbacks *callbacks) {
  for (size_t i = 0; i < callbacks->len; ++i) {
    free(callbacks->cmds[i]);
  }
}

void callbacks_append(Callbacks *callbacks, char *cmd) {
  if (!callbacks->cmds) {
    callbacks->cmds = malloc(10 * sizeof(char *));
    callbacks->allocated = 10;
  } else if (callbacks->len <= callbacks->allocated) {
    size_t new_size = callbacks->len + 10;
    callbacks->cmds = realloc(callbacks->cmds, new_size * sizeof(char *));
    callbacks->allocated = new_size;
  }

  callbacks->cmds[callbacks->len++] = cmd;
}

void callbacks_dup_append(Callbacks *callbacks, char *cmd) {
  callbacks_append(callbacks, strdup(cmd));
}

void callbacks_exec(Callbacks *callbacks) {
  if (!callbacks) {
    return;
  }

  for (size_t i = 0; i < callbacks->len; ++i) {
    daemonize(callbacks->cmds[i]);
  }
}

int callbacks_inspect(Callbacks *callbacks, int (*printer)(void *, const char *, ...), void *arg) {
  int sum = 0;
  sum += printer(arg, "%ld: [", callbacks->timeout);
  for (size_t i = 0; i < callbacks->len; ++i) {
    if (i != 0) {
      sum += printer(arg, ", ");
    }
    sum += printer(arg, "\"%s\"", callbacks->cmds[i]);
  }
  sum += printer(arg, "]");

  return sum;
}

Timeouts *timeouts_new(void) {
  return calloc(1, sizeof(Timeouts));
}

inline size_t timeouts_len(Timeouts *timeouts) {
  return timeouts->len;
}

void timeouts_shrink_to_fit(Timeouts *timeouts) {
  if (timeouts->len) {
    if (timeouts->callbacks) {
      free(timeouts->callbacks);
      timeouts->callbacks = NULL;
    }
  } else {
    timeouts->callbacks = realloc(timeouts->callbacks, timeouts->len * sizeof(Callbacks *));
  }
  timeouts->allocated = timeouts->len;

  for (size_t i = 0; i < timeouts->len; ++i) {
    callbacks_shrink_to_fit(&timeouts->callbacks[i]);
  }
}

void timeouts_free(Timeouts *timeouts) {
  if (!timeouts) {
    return;
  }

  for (size_t i = 0; i < timeouts->len; ++i) {
    _callbacks_free(&timeouts->callbacks[i]);
  }
  free(timeouts);
}

void timeouts_ensure_alloc(Timeouts *timeouts, size_t new_len) {
  if (!timeouts->callbacks) {
    timeouts->callbacks = malloc(10 * sizeof(Callbacks));
    timeouts->allocated = 10;
  } else if (new_len > timeouts->allocated) {
    size_t new_alloc = new_len / 10 + ((new_len % 10 != 0) ? 1 : 0);
    timeouts->callbacks = realloc(timeouts->callbacks, new_alloc * sizeof(Callbacks));
    timeouts->allocated = new_alloc;
  }
}

void timeouts_insert_at(Timeouts *timeouts, size_t pos, time_t time) {
  timeouts_ensure_alloc(timeouts, timeouts->len + 1);
  if (pos < timeouts->len) {
    memmove(timeouts->callbacks + pos + 1, timeouts->callbacks + pos, (timeouts->len - pos) * sizeof(Callbacks));
  } else {
    pos = timeouts->len; // force last position
  }
  memset(timeouts->callbacks + (pos * sizeof(Callbacks)), 0, sizeof(Callbacks));
  timeouts->callbacks[pos].timeout = time;
  timeouts->len++;
}

Callbacks *timeouts_get_or_create(Timeouts *timeouts, time_t time) {
  if (!timeouts->callbacks) {
    timeouts_insert_at(timeouts, 0, time);
    return &timeouts->callbacks[0];
  } else {
    size_t p = 0, r = timeouts->len - 1;

    if (time < timeouts->callbacks[0].timeout) {
      timeouts_insert_at(timeouts, 0, time);
    } else if (time > timeouts->callbacks[r].timeout) {
      timeouts_insert_at(timeouts, timeouts->len, time);
    } else {
      while (p <= r) {
        size_t q = (p + r) / 2;
        if (timeouts->callbacks[q].timeout == time) {
          return &timeouts->callbacks[q];
        }

        if (timeouts->callbacks[q].timeout > time) {
          r = q - 1;
        } else {
          p = q + 1;
        }
      }

      timeouts_insert_at(timeouts, p, time);
    }

    return &timeouts->callbacks[p];
  }
}

void timeouts_append(Timeouts *timeouts, time_t time, char *cmd) {
  callbacks_append(timeouts_get_or_create(timeouts, time), cmd);
}

void timeouts_dup_append(Timeouts *timeouts, time_t time, char *cmd) {
  callbacks_dup_append(timeouts_get_or_create(timeouts, time), cmd);
}

Callbacks *timeouts_get(Timeouts *timeouts, time_t time) {
  if (!timeouts->callbacks) {
    return NULL;
  }
  size_t p = 0, r = timeouts->len - 1;
  if (time < timeouts->callbacks[0].timeout || time > timeouts->callbacks[r].timeout) {
    return NULL;
  } else {
    while (p <= r) {
      size_t q = (p + r) / 2;
      if (timeouts->callbacks[q].timeout == time) {
        return &timeouts->callbacks[q];
      }

      if (timeouts->callbacks[q].timeout > time) {
        r = q - 1;
      } else {
        p = q + 1;
      }
    }

    return NULL;
  }
}

int timeouts_inspect(Timeouts *timeouts, int (*printer)(void *, const char *, ...), void *arg) {
  int sum = 0;
  sum += printer(arg, "{");
  for (size_t i = 0; i < timeouts->len; ++i) {
    if (i != 0) {
      sum += printer(arg, ", ");
    }
    sum += callbacks_inspect(&timeouts->callbacks[i], printer, arg);
  }
  sum += printer(arg, "}");

  return 0;
}

void timeouts_exec_reset(Timeouts *timeouts) {
  callbacks_exec(timeouts_get(timeouts, 0));
}

void timeouts_exec(Timeouts *timeouts, time_t from, time_t to) {
  if (!timeouts->callbacks) {
    return;
  }

  for (size_t i = 0; i < timeouts->len; ++i) {
    Callbacks *callbacks = &timeouts->callbacks[i];
    if (callbacks->timeout == 0) {
      continue;
    }

    if (callbacks->timeout > to) {
      break;
    }

    if (callbacks->timeout > from) {
      callbacks_exec(callbacks);
    }
  }
}