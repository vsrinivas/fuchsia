#include "libc.h"
#include "pthread_impl.h"

#include <signal.h>

int sigsuspend(const sigset_t* mask) {
    return __rt_sigsuspend(mask, _NSIG / 8);
}
