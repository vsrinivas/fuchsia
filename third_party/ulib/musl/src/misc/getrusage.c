#include "syscall.h"
#include <sys/resource.h>

int getrusage(int who, struct rusage* ru) {
    return syscall(SYS_getrusage, who, ru);
}
