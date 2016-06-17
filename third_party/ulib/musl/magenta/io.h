#pragma once

#include <runtime/compiler.h>
#include <stdint.h>
#include <sys/types.h>

/* LK specific calls to register to get input/output of the main console */

__BEGIN_CDECLS

/* the underlying handle to talk to io devices */
struct io_handle;
typedef struct io_handle_hooks {
    ssize_t (*write)(struct io_handle* handle, const char* buf, size_t len);
    ssize_t (*read)(struct io_handle* handle, char* buf, size_t len);
} io_handle_hooks_t;

#define IO_HANDLE_MAGIC (0x696f6820) // "ioh "

typedef struct io_handle {
    uint32_t magic;
    const io_handle_hooks_t* hooks;
} io_handle_t;

/* routines to call through the io handle */
ssize_t io_write(io_handle_t* io, const char* buf, size_t len);
ssize_t io_read(io_handle_t* io, char* buf, size_t len);

/* initialization routine */
#define IO_HANDLE_INITIAL_VALUE(_hooks) \
    { .magic = IO_HANDLE_MAGIC, .hooks = _hooks }

static inline void io_handle_init(io_handle_t* io, io_handle_hooks_t* hooks) {
    *io = (io_handle_t)IO_HANDLE_INITIAL_VALUE(hooks);
}

__END_CDECLS
