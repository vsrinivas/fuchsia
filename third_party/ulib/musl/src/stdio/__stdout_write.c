#include "stdio_impl.h"
#include <unistd.h>

size_t __stdout_write(FILE* f, const unsigned char* buf, size_t len) {
    f->write = __stdio_write;
    if (!(f->flags & F_SVB) && !isatty(f->fd)) f->lbf = -1;
    return __stdio_write(f, buf, len);
}
