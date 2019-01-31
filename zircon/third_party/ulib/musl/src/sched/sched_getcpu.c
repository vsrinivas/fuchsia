#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>

int sched_getcpu(void) {
    // TODO(kulakowski) implement this on zircon
    errno = ENOSYS;
    return -1;
}
