#include <errno.h>
#include <sys/resource.h>

int getrlimit(int resource, struct rlimit* rlim) {
    // TODO(kulakowski) implement getrlimit
    errno = ENOSYS;
    return -1;
}
