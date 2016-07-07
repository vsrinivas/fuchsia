#include <unistd.h>

#include <errno.h>

int __dup3(int old, int new, int flags) {
    errno = ENOSYS;
    return -1;
}

weak_alias(__dup3, dup3);
