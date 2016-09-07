#define _GNU_SOURCE
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>

#include "libc.h"

pid_t __vfork(void) {
    // TODO(kulakowski) Some level of vfork emulation.
    errno = ENOSYS;
    return -1;
}

weak_alias(__vfork, vfork);
