#include <errno.h>
#include <signal.h>

#include "pthread_impl.h"

int sigaltstack(const stack_t* restrict ss, stack_t* restrict old) {
    if (ss) {
        if (ss->ss_size < MINSIGSTKSZ) {
            errno = ENOMEM;
            return -1;
        }
        if (ss->ss_flags & ~SS_DISABLE) {
            errno = EINVAL;
            return -1;
        }
    }
    return __sigaltstack(ss, old);
}
