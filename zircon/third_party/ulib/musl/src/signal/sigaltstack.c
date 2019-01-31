#include <errno.h>
#include <signal.h>

int sigaltstack(const stack_t* restrict ss, stack_t* restrict old) {
    errno = ENOSYS;
    return -1;
}
