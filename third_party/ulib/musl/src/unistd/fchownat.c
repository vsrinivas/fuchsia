#include "syscall.h"
#include <unistd.h>

int fchownat(int fd, const char* path, uid_t uid, gid_t gid, int flag) {
    return syscall(SYS_fchownat, fd, path, uid, gid, flag);
}
