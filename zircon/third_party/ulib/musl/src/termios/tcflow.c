#include <sys/ioctl.h>
#include <termios.h>

int tcflow(int fd, int action) {
    return ioctl(fd, TCXONC, action);
}
