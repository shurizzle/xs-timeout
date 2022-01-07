#include "timeouts.h"
#include "options.h"
#include <alloca.h>
#include <limits.h>
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

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
        {0, 0, 0, 0},
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "h", long_options, &option_index);

    if (c == -1) {
      break;
    }
    switch (c) {
    case 0:
    case 'h':
      return (Options) {.help=true,.timeouts=NULL};
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
    return (Options) {.help=false,.timeouts=NULL};
  }

  return (Options) {.help=false,.timeouts=ts};
}

#define TIME_MAX (((time_t)1 << (sizeof(time_t) * CHAR_BIT - 2)) - 1) * 2 + 1

bool _parse_timeout(char *timeout, time_t *time, char **cmd) {
  unsigned long long tmp;
  char *endptr = NULL;

  tmp = strtoull(timeout, &endptr, 10);
  if (tmp == ULLONG_MAX && errno == ERANGE) {
    fprintf(stderr, "'%s` is not a valid timeout\n", timeout);
    return false;
  }
  if (tmp > TIME_MAX) {
    fprintf(stderr, "'%s` is not a valid timeout\n", timeout);
    return false;
  }

  *time = (time_t)tmp;

  if (*endptr != ':') {
    fprintf(stderr, "'%s` is not a valid timeout\n", timeout);
    return false;
  }
  endptr++;
  *cmd = endptr;

  while (*endptr && isspace(*endptr)) {
    endptr++;
  }
  if (!*endptr) {
    fprintf(stderr, "'%s` is not a valid timeout\n", timeout);
    return false;
  }

  return true;
}

bool parse_timeout(Timeouts *timeouts, char *t) {
  time_t time;
  char *cmd;

  if (starts_with(t, "reset:")) {
    time = 0;
    cmd = t + (6 * sizeof(char));

    char *endptr = cmd;
    while (*endptr && isspace(*endptr)) {
      endptr++;
    }
    if (!*endptr) {
      fprintf(stderr, "'%s` is not a valid reset\n", t);
      return false;
    }
  } else {
    if (!_parse_timeout(t, &time, &cmd)) {
      fprintf(stderr, "'%s` is not a valid timeout\n", t);
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
