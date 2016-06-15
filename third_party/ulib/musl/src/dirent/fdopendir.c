#include "__dirent.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

DIR* fdopendir(int fd) {
    DIR* dir;
    struct stat st;

    if (fstat(fd, &st) < 0) {
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return 0;
    }
    if (!(dir = calloc(1, sizeof *dir))) {
        return 0;
    }

    fcntl(fd, F_SETFD, FD_CLOEXEC);
    dir->fd = fd;
    return dir;
}
