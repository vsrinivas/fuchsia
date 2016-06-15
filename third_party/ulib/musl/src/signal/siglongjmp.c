#include "pthread_impl.h"
#include "syscall.h"
#include <setjmp.h>
#include <signal.h>

_Noreturn void siglongjmp(sigjmp_buf buf, int ret) {
    longjmp(buf, ret);
}
