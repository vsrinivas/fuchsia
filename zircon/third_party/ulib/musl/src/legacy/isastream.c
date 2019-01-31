#include <fcntl.h>
#include <stropts.h>

int isastream(int fd) {
    return fcntl(fd, F_GETFD) < 0 ? -1 : 0;
}
