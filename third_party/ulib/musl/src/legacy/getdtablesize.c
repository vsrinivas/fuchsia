#define _GNU_SOURCE
#include <limits.h>
#include <sys/resource.h>
#include <unistd.h>

int getdtablesize(void) {
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    return rl.rlim_max < INT_MAX ? rl.rlim_max : INT_MAX;
}
