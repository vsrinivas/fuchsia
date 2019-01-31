#include <sys/times.h>

#include <errno.h>

clock_t times(struct tms* tms) {
    errno = ENOSYS;
    return -1;
}
