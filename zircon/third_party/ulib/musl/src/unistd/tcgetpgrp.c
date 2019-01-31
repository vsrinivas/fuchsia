#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

pid_t tcgetpgrp(int fd) {
    int pgrp;
    if (ioctl(fd, TIOCGPGRP, &pgrp) < 0)
        return -1;
    return pgrp;
}
