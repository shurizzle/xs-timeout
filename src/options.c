#include "options.h"
#include "timeouts.h"
#include "util.h"
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>

bool starts_with(const char *str, const char *pre) {
  return strncmp(pre, str, strlen(pre)) == 0;
}

Timeouts *parse_timeouts(char **timeouts, size_t timeouts_len);

Options parse_options(int argc, char **argv) {
  int c;
  char **timeouts = alloca(argc * sizeof(char *));
  size_t timeouts_len = 0;

  while (1) {
    static struct option long_options[] = {
        {"help", no_argument, NULL, 0},
        {"version", no_argument, NULL, 0},
        {0, 0, 0, 0},
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "hv", long_options, &option_index);

    if (c == -1) {
      break;
    }
    switch (c) {
    case 0:
      if (option_index == 0) {
        goto help;
      } else if (option_index == 1) {
        goto version;
      }
      break;
    case 'h':
      goto help;
    case 'v':
      goto version;
    case '?':
      break;
    default:
      timeouts[timeouts_len++] = argv[optind];
      break;
    }
  }

  for (int i = optind; i < argc; ++i) {
    timeouts[timeouts_len++] = argv[i];
  }

  Timeouts *ts = parse_timeouts(timeouts, timeouts_len);
  if (!ts) {
    return (Options){.help = false, .version = false, .timeouts = NULL};
  }

  return (Options){.help = false, .version = false, .timeouts = ts};
help:
  return (Options){.help = true, .version = false, .timeouts = NULL};
version:
  return (Options){.help = false, .version = true, .timeouts = NULL};
}

#define TIMEOUT_MAX UINT32_MAX / 1000

bool _parse_timeout(char *timeout, uint32_t *time, char **cmd) {
  unsigned long long tmp;
  char *endptr = NULL;

  tmp = strtoull(timeout, &endptr, 10);
  if (tmp == ULLONG_MAX && errno == ERANGE) {
    eprintf("'%s` is not a valid timeout\n", timeout);
    return false;
  }
  if (tmp > TIMEOUT_MAX) {
    eprintf("'%s` is not a valid timeout\n", timeout);
    return false;
  }

  *time = (uint32_t)tmp;

  if (*endptr != ':') {
    eprintf("'%s` is not a valid timeout\n", timeout);
    return false;
  }
  endptr++;
  *cmd = endptr;

  while (*endptr && isspace(*endptr)) {
    endptr++;
  }
  if (!*endptr) {
    eprintf("'%s` is not a valid timeout\n", timeout);
    return false;
  }

  return true;
}

bool parse_timeout(Timeouts *timeouts, char *t) {
  uint32_t time;
  char *cmd;

  if (starts_with(t, "reset:")) {
    time = 0;
    cmd = t + (6 * sizeof(char));

    char *endptr = cmd;
    while (*endptr && isspace(*endptr)) {
      endptr++;
    }
    if (!*endptr) {
      eprintf("'%s` is not a valid reset\n", t);
      return false;
    }
  } else {
    if (!_parse_timeout(t, &time, &cmd)) {
      eprintf("'%s` is not a valid timeout\n", t);
      return false;
    }
  }

  timeouts_dup_append(timeouts, time, cmd);
  return true;
}

Timeouts *parse_timeouts(char **timeouts, size_t timeouts_len) {
  if (!timeouts_len) {
    return NULL;
  }

  Timeouts *res = timeouts_new();

  for (size_t i = 0; i < timeouts_len; ++i) {
    if (!parse_timeout(res, timeouts[i])) {
      timeouts_free(res);
      return NULL;
    }
  }

  if (!res->len) {
    timeouts_free(res);
    res = NULL;
  }

  return res;
}
