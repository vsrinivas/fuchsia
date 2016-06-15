#include "libc.h"
#include <sys/resource.h>

int setrlimit(int resource, const struct rlimit* rlim) {
    // TODO(kulakowski) Implement fake rlimit.
    return -1;
}

LFS64(setrlimit);
