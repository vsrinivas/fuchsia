#include <errno.h>
#include <sys/resource.h>

int setpriority(int which, id_t who, int prio) {
    errno = ENOSYS;
    return -1;
}
