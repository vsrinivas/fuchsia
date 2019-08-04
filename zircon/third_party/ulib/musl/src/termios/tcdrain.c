#include <errno.h>
#include <termios.h>

int tcdrain(int fd) {
  // TODO(kulakowski) terminal handling.
  errno = ENOSYS;
  return -1;
}
