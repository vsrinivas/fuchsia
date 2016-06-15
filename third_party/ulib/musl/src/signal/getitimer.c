#include "syscall.h"
#include <sys/time.h>

int getitimer(int which, struct itimerval* old) {
    return syscall(SYS_getitimer, which, old);
}
