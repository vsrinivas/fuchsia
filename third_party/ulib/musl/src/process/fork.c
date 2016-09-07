#include "libc.h"
#include "pthread_impl.h"
#include "syscall.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>

static void dummy(int x) {}

weak_alias(dummy, __fork_handler);

pid_t fork(void) {
    pid_t ret;
    sigset_t set;
    __fork_handler(-1);
    __block_all_sigs(&set);
    // TODO(kulakowski) Some level of fork emulation.
    ret = ENOSYS;

    if (!ret) {
        // TODO(kulakowski): NB: fork assumes that the calling thread
        // is a pthread, and that the created thread in the new
        // process will therefore also be a pthread.
        pthread_t self = __pthread_self();
        if (self == NULL)
            __builtin_trap();
    }
    __restore_sigs(&set);
    __fork_handler(!ret);
    return ret;
}
