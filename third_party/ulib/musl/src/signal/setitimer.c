#include "syscall.h"
#include <sys/time.h>

int setitimer(int which, const struct itimerval* restrict new, struct itimerval* restrict old) {
    return syscall(SYS_setitimer, which, new, old);
}
