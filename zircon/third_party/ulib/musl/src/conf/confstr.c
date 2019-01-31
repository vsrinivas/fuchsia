#include <errno.h>
#include <stdio.h>
#include <unistd.h>

size_t confstr(int name, char* buf, size_t len) {
    const char* s = "";
    if (!name) {
        s = "/bin:/usr/bin";
    } else if ((name & ~4U) != 1 &&
               name - _CS_POSIX_V6_ILP32_OFF32_CFLAGS < 0 &&
               name - _CS_POSIX_V6_ILP32_OFF32_CFLAGS > 31) {
        errno = EINVAL;
        return 0;
    }
    // snprintf is overkill but avoid wasting code size to implement
    // this completely useless function and its truncation semantics
    return snprintf(buf, len, "%s", s) + 1;
}
