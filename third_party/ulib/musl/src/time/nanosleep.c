#include <time.h>

#include <magenta/syscalls.h>

int nanosleep(const struct timespec* req, struct timespec* rem) {
    // |rem| is currently unused in magenta, as it is only to be
    // filled in on EINTR, which can't (yet!) happen.

    mx_time_t nanos = req->tv_sec * 1000000000ull;
    nanos += req->tv_nsec;

    return _magenta_nanosleep(nanos);
}
