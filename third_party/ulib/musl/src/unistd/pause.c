#include "libc.h"
#include "syscall.h"
#include <signal.h>
#include <unistd.h>

int pause(void) {
#ifdef SYS_pause
    return syscall(SYS_pause);
#else
    return syscall(SYS_ppoll, 0, 0, 0, 0);
#endif
}
