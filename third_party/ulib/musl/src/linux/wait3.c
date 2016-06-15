#define _GNU_SOURCE
#include "syscall.h"
#include <sys/resource.h>
#include <sys/wait.h>

pid_t wait3(int* status, int options, struct rusage* usage) {
    return wait4(-1, status, options, usage);
}
