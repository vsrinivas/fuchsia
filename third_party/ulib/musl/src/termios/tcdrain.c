#include "libc.h"
#include "syscall.h"
#include <sys/ioctl.h>
#include <termios.h>

int tcdrain(int fd) {
    return syscall(SYS_ioctl, fd, TCSBRK, 1);
}
