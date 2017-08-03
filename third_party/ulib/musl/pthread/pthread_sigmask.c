#include <errno.h>
#include <signal.h>

#include "pthread_impl.h"

int pthread_sigmask(int how, const sigset_t* restrict set, sigset_t* restrict old) {
    if ((unsigned)how - SIG_BLOCK > 2U)
        return EINVAL;
    if (old) {
        if (sizeof old->__bits[0] == 8) {
            old->__bits[0] &= ~0x380000000ULL;
        } else {
            old->__bits[0] &= ~0x80000000UL;
            old->__bits[1] &= ~0x3UL;
        }
    }
    return 0;
}
