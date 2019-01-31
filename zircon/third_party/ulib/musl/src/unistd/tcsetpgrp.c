#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int tcsetpgrp(int fd, pid_t pgrp) {
    int pgrp_int = pgrp;
    return ioctl(fd, TIOCSPGRP, &pgrp_int);
}
