#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/stat.h>

int lchmod(const char* path, mode_t mode) {
    return fchmodat(AT_FDCWD, path, mode, AT_SYMLINK_NOFOLLOW);
}
