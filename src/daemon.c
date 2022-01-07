#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int daemonize(char *cmd) {
  pid_t pid;

  pid = fork();

  if (pid < 0) {
    return pid;
  }

  if (pid > 0) {
    int status;
    int res = waitpid(pid, &status, 0);
    if (res < 0) {
      return res;
    }
    return status;
  }

  if (setsid() < 0) {
    return -1;
  }

  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);

  pid = fork();

  if (pid < 0) {
    exit(pid);
  }

  if (pid > 0) {
    exit(0);
  }

  umask(0);

  for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
    if (x != fileno(stdout) && x != fileno(stderr)) {
      close(x);
    }
  }

  exit(execl("/bin/sh", "/bin/sh", "-c", cmd, NULL));
}
