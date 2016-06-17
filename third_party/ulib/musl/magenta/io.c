#include "io.h"

#include <assert.h>

#define ERR_NOT_SUPPORTED (-24)

ssize_t io_write(io_handle_t* io, const char* buf, size_t len) {
    assert(io->magic == IO_HANDLE_MAGIC);

    if (!io->hooks->write)
        return ERR_NOT_SUPPORTED;

    return io->hooks->write(io, buf, len);
}

ssize_t io_read(io_handle_t* io, char* buf, size_t len) {
    assert(io->magic == IO_HANDLE_MAGIC);

    if (!io->hooks->read)
        return ERR_NOT_SUPPORTED;

    return io->hooks->read(io, buf, len);
}
