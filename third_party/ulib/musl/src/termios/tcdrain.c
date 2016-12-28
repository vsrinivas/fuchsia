#include <termios.h>

#include <errno.h>

int tcdrain(int fd) {
    // TODO(kulakowski) terminal handling.
    errno = ENOSYS;
    return -1;
}
