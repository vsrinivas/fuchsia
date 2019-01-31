#include <stdio.h>
#include <unistd.h>

int remove(const char* path) {
    // zircon's unlink(2) works on all filesystem objects, including
    // directories, so there is no need to check to see whether we
    // must call rmdir.
    return unlink(path);
}
