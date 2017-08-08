#include "libc.h"
#include "pthread_impl.h"
#include <string.h>
#include <unistd.h>

static void dummy(int x) {}

weak_alias(dummy, __fork_handler);

pid_t fork(void) {
    __fork_handler(-1);
    __fork_handler(0);
    errno = ENOSYS;
    return -1;
}
