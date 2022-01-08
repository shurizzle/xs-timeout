# xs-timeout

A simple tool that executes commands on user idle.

Just run it as:

```bash
xs-timeout '60:light -S 40' '120:betterlockscreen -l dimblur' 'reset:light -S 100'
```

It dim the light at 40% after 60 seconds and start betterlockscreen after 120.
On the next user interaction (reset) it set light back to 100%.

You can set all the timeouts and resets you want to, repetitions included.

Every command will be launched as a command by /bin/sh after a double fork of the process with stdin closed, so everything will be logged on stdout/stderr.

xs-timeout supports some signals:

- SIGSTOP, SIGTSTP (^Z) will close the X11 connection and pause the program, you can continue the normal execution with a SIGCONT
- SIGCONT will re-open the X11 connection and call resets
- SIGALRM will restart the timers, so resets all called

It can be useful if you want to implements something like caffeine/caffeinate.

---

Enjoy :D
