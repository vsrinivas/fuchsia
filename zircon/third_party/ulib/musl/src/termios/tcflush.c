#include <sys/ioctl.h>
#include <termios.h>

int tcflush(int fd, int queue) {
    return ioctl(fd, TCFLSH, queue);
}
