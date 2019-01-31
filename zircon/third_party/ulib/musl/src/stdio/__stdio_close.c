#include "stdio_impl.h"
#include <unistd.h>

int __stdio_close(FILE* f) {
    return close(f->fd);
}
