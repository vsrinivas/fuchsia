#include "syscall.h"
#include <sys/utsname.h>

int uname(struct utsname* uts) {
    return syscall(SYS_uname, uts);
}
