#include <errno.h>
#include <sys/resource.h>

int getrusage(int who, struct rusage* ru) {
    errno = ENOSYS;
    return -1;
}
