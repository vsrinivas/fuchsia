#include "pthread_impl.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>

int raise(int sig) {
    int ret;
    sigset_t set;
    __block_app_sigs(&set);
    // TODO(kulakowski) Signals.
    ret = ENOSYS;
    __restore_sigs(&set);
    return ret;
}
