#include "pthread_impl.h"

#include <setjmp.h>
#include <signal.h>

__attribute__((__visibility__("hidden"))) int __sigsetjmp_tail(sigjmp_buf jb, int ret) {
    void* p = jb->__ss;
    __rt_sigprocmask(SIG_SETMASK, ret ? p : 0, ret ? 0 : p, _NSIG / 8);
    return ret;
}
