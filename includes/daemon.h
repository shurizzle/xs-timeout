#ifndef __XS_DAEMON__
#define __XS_DAEMON__

#include <stdio.h> /* fix an error in clangd */

int daemonize(char *cmd);

#endif
