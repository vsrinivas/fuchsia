#include <sys/ioctl.h>
#include <termios.h>

pid_t tcgetsid(int fd) {
    int sid;
    if (ioctl(fd, TIOCGSID, &sid) < 0) return -1;
    return sid;
}
