#include <unistd.h>

#include "libc.h"

int execv(const char* path, char* const argv[]) {
    return execve(path, argv, __environ);
}
