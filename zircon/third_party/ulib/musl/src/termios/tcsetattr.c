#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>

int tcsetattr(int fd, int act, const struct termios* tio) {
    if (act < 0 || act > 2) {
        errno = EINVAL;
        return -1;
    }
    return ioctl(fd, TCSETS + act, tio);
}
