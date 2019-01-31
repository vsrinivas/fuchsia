#include <sys/ioctl.h>
#include <termios.h>

int tcsendbreak(int fd, int dur) {
    /* nonzero duration is implementation-defined, so ignore it */
    return ioctl(fd, TCSBRK, 0);
}
