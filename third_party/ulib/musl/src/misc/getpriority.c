#include <errno.h>
#include <sys/resource.h>

int getpriority(int which, id_t who) {
    errno = ENOSYS;
    return -1;
}
