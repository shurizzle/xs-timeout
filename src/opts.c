#include "callbacks.h"
#include <alloca.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct options {
  bool help;
  callbacks *callbacks;
  size_t callbacks_len;
  char *reset;
} options;

bool starts_with(const char *str, const char *pre) {
  return strncmp(pre, str, strlen(pre)) == 0;
}

options *parse_options(int argc, char **argv) {
  int c;
  char **timeouts = alloca(argc * sizeof(char *));
  size_t timeouts_len = 0;
  char *reset = NULL;
  options *opts = NULL;

  while (1) {
    static struct option long_options[] = {
        {"help", no_argument, NULL, 0},
        {0, 0, 0, 0},
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "h", long_options, &option_index);

    if (c == 1) {
      break;
    }
    switch (c) {
    case 0:
    case 'h':
      opts = calloc(1, sizeof(options));
      opts->help = true;
      return opts;
    case '?':
      break;
    default:
      if (starts_with(argv[optind], "reset:")) {
        if (!reset) {
          fprintf(stderr, "You can have only a reset callback");
          return NULL;
        }
      } else {
        timeouts[timeouts_len++] = argv[optind];
      }
      return NULL;
    }
  }

  for (int i = optind; i < argc; ++i) {
    if (starts_with(argv[i], "reset:")) {
      if (!reset) {
        fprintf(stderr, "You can have only a reset callback");
        return NULL;
      }
    } else {
      timeouts[timeouts_len++] = argv[i];
    }
  }

  opts = calloc(1, sizeof(options));

  return opts;
}

bool parse_timeout(char *timeout) {
  time_t time = 0;
  char *cmd = NULL;
}

void free_options(options *opts) {
  if (opts->callbacks) {
    for (int i = 0; i < opts->callbacks_len; ++i) {
      callbacks *callbacks = &opts->callbacks[i];

      for (int j = 0; j < callbacks->cmds_len; ++j) {
        free(callbacks->cmds[j]);
      }
      callbacks->cmds = NULL;
      callbacks->cmds_len = 0;
    }
    free(opts->callbacks);
    opts->callbacks = NULL;
    opts->callbacks_len = 0;
  }

  if (opts->reset) {
    free(opts->reset);
  }

  free(opts);
}
