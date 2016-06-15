#include "stdio_impl.h"

off_t __stdio_seek(FILE* f, off_t off, int whence) {
    return (off_t)-1;
}
