#include <sys/wait.h>

#include <errno.h>

pid_t waitpid(pid_t pid, int* status, int options) {
    // TODO(kulakowski) Actually wait on |pid|.
    errno = ENOSYS;
    return -1;
}
