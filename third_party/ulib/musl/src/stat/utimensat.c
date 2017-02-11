#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>

int utimensat(int fd, const char* path, const struct timespec times[2], int flags) {
    errno = ENOSYS;
    return -1;
}
