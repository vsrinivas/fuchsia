// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <fdio/limits.h>
#include <fdio/remoteio.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <threads.h>

typedef struct fdio fdio_t;
typedef struct fdio_namespace fdio_ns_t;

// FDIO provides open/close/read/write io over various transports
// via the fdio_t interface abstraction.
//
// The PIPE protocol uses message ports as simple, no-flow-control
// io pipes with a maximum message size of ZX_PIPE_SIZE.
//
// The REMOTEIO protocol uses message ports to implement simple
// synchronous remoting of read/write/close operations.
//
// The NULL protocol absorbs writes and is never readable.

typedef struct fdio_ops {
    ssize_t (*read)(fdio_t* io, void* data, size_t len);
    ssize_t (*read_at)(fdio_t* io, void* data, size_t len, off_t offset);
    ssize_t (*write)(fdio_t* io, const void* data, size_t len);
    ssize_t (*write_at)(fdio_t* io, const void* data, size_t len, off_t offset);
    ssize_t (*recvfrom)(fdio_t* io, void* data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen);
    ssize_t (*sendto)(fdio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen);
    ssize_t (*recvmsg)(fdio_t* io, struct msghdr* msg, int flags);
    ssize_t (*sendmsg)(fdio_t* io, const struct msghdr* msg, int flags);
    off_t (*seek)(fdio_t* io, off_t offset, int whence);
    zx_status_t (*misc)(fdio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* data, size_t len);
    zx_status_t (*close)(fdio_t* io);
    zx_status_t (*open)(fdio_t* io, const char* path, uint32_t flags, uint32_t mode, fdio_t** out);
    zx_status_t (*clone)(fdio_t* io, zx_handle_t* out_handles, uint32_t* out_types);
    zx_status_t (*unwrap)(fdio_t* io, zx_handle_t* out_handles, uint32_t* out_types);
    zx_status_t (*shutdown)(fdio_t* io, int how);
    void (*wait_begin)(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* signals);
    void (*wait_end)(fdio_t* io, zx_signals_t signals, uint32_t* events);
    ssize_t (*ioctl)(fdio_t* io, uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);
    ssize_t (*posix_ioctl)(fdio_t* io, int req, va_list va);
    zx_status_t (*get_vmo)(fdio_t* io, int flags, zx_handle_t* out);
} fdio_ops_t;

// fdio_t ioflag values
#define IOFLAG_CLOEXEC              (1 << 0)
#define IOFLAG_SOCKET               (1 << 1)
#define IOFLAG_EPOLL                (1 << 2)
#define IOFLAG_WAITABLE             (1 << 3)
#define IOFLAG_SOCKET_CONNECTING    (1 << 4)
#define IOFLAG_SOCKET_CONNECTED     (1 << 5)
#define IOFLAG_NONBLOCK             (1 << 6)

// The subset of fdio_t per-fd flags queryable via fcntl.
// Static assertions in unistd.c ensure we aren't colliding.
#define IOFLAG_FD_FLAGS IOFLAG_CLOEXEC

typedef struct fdio {
    fdio_ops_t* ops;
    uint32_t magic;
    atomic_int_fast32_t refcount;
    int32_t dupcount;
    uint32_t ioflag;
} fdio_t;

// Lifecycle notes:
//
// Upon creation, fdio objects have a refcount of 1.
// fdio_acquire() and fdio_release() are used to upref
// and downref, respectively.  Upon downref to 0,
// fdio_free() is called, which poisons the object and
// free()s it.
//
// The close hook must be called before free and should
// only be called once.  In normal use, fdio objects are
// accessed through the fdio_fdtab, and when close is
// called they are removed from the fdtab and the reference
// that the fdtab itself is holding is released, at which
// point they will be free()'d unless somebody is holding
// a ref due to an ongoing io transaction, which will
// certainly fail doe to underlying handles being closed
// at which point a downref will happen and destruction
// will follow.
//
// dupcount tracks how many fdtab entries an fdio object
// is in.  close() reduces the dupcount, and only actually
// closes the underlying object when it reaches zero.

#define FDIO_MAGIC 0x4f49584d // FDIO

static inline ssize_t fdio_read(fdio_t* io, void* data, size_t len) {
    return io->ops->read(io, data, len);
}
static inline ssize_t fdio_read_at(fdio_t* io, void* data, size_t len, off_t offset) {
    return io->ops->read_at(io, data, len, offset);
}
static inline ssize_t fdio_write(fdio_t* io, const void* data, size_t len) {
    return io->ops->write(io, data, len);
}
static inline ssize_t fdio_write_at(fdio_t* io, const void* data, size_t len, off_t offset) {
    return io->ops->write_at(io, data, len, offset);
}
static inline off_t fdio_seek(fdio_t* io, off_t offset, int whence) {
    return io->ops->seek(io, offset, whence);
}
static inline zx_status_t fdio_misc(fdio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* data, size_t len) {
    return io->ops->misc(io, op, off, maxreply, data, len);
}
static inline zx_status_t fdio_open(fdio_t* io, const char* path, uint32_t flags, uint32_t mode, fdio_t** out) {
    return io->ops->open(io, path, flags, mode, out);
}
zx_status_t fdio_close(fdio_t* io);
zx_status_t fdio_wait(fdio_t* io, uint32_t events, zx_time_t deadline,
                      uint32_t* out_pending);

// Wraps a socket with an fdio_t using simple io.
// Takes ownership of h.
fdio_t* fdio_pipe_create(zx_handle_t h);

zx_status_t fdio_pipe_posix_ioctl(fdio_t* io, int req, va_list va);

// Wraps a vmo, offset, length with an fdio_t providing a readonly file.
// Takens ownership of h.
fdio_t* fdio_vmofile_create(zx_handle_t h, zx_off_t off, zx_off_t len);

// Wraps a socket with an fdio_t using socket io.
fdio_t* fdio_socket_create(zx_handle_t s, int flags);

// creates a message port and pair of simple io fdio_t's
int fdio_pipe_pair(fdio_t** a, fdio_t** b);

void fdio_free(fdio_t* io);

fdio_t* fdio_ns_open_root(fdio_ns_t* ns);

// io will be consumed by this and must not be shared
void fdio_chdir(fdio_t* io, const char* path);

// Wraps an arbitrary handle with a fdio_t that works with wait hooks.
// Takes ownership of handle unless shared_handle is true.
fdio_t* fdio_waitable_create(zx_handle_t h, zx_signals_t signals_in, zx_signals_t signals_out, bool shared_handle);

void fdio_socket_set_stream_ops(fdio_t* io);
void fdio_socket_set_dgram_ops(fdio_t* io);

zx_status_t fdio_socket_posix_ioctl(fdio_t* io, int req, va_list va);
zx_status_t fdio_socket_shutdown(fdio_t* io, int how);
zx_status_t fdio_socketpair_shutdown(fdio_t* io, int how);

// unsupported / do-nothing hooks shared by implementations
ssize_t fdio_default_read(fdio_t* io, void* _data, size_t len);
ssize_t fdio_default_read_at(fdio_t* io, void* _data, size_t len, off_t offset);
ssize_t fdio_default_write(fdio_t* io, const void* _data, size_t len);
ssize_t fdio_default_write_at(fdio_t* io, const void* _data, size_t len, off_t offset);
ssize_t fdio_default_recvfrom(fdio_t* io, void* _data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen);
ssize_t fdio_default_sendto(fdio_t* io, const void* _data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen);
ssize_t fdio_default_recvmsg(fdio_t* io, struct msghdr* msg, int flags);
ssize_t fdio_default_sendmsg(fdio_t* io, const struct msghdr* msg, int flags);
off_t fdio_default_seek(fdio_t* io, off_t offset, int whence);
zx_status_t fdio_default_misc(fdio_t* io, uint32_t op, int64_t off, uint32_t arg, void* data, size_t len);
zx_status_t fdio_default_close(fdio_t* io);
zx_status_t fdio_default_open(fdio_t* io, const char* path, uint32_t flags, uint32_t mode, fdio_t** out);
zx_status_t fdio_default_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types);
ssize_t fdio_default_ioctl(fdio_t* io, uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);
void fdio_default_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals);
void fdio_default_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events);
zx_status_t fdio_default_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types);
zx_status_t fdio_default_shutdown(fdio_t* io, int how);
ssize_t fdio_default_posix_ioctl(fdio_t* io, int req, va_list va);
zx_status_t fdio_default_get_vmo(fdio_t* io, int flags, zx_handle_t* out);

void __fdio_startup_handles_init(uint32_t num, zx_handle_t handles[],
                                 uint32_t handle_info[])
    __attribute__((visibility("hidden")));

void __fdio_rchannel_init(void) __attribute__((visibility("hidden")));

typedef struct {
    mtx_t lock;
    mtx_t cwd_lock;
    bool init;
    mode_t umask;
    fdio_t* root;
    fdio_t* cwd;
    fdio_t* fdtab[FDIO_MAX_FD];
    fdio_ns_t* ns;
    char cwd_path[PATH_MAX];
} fdio_state_t;

extern fdio_state_t __fdio_global_state;

#define fdio_lock (__fdio_global_state.lock)
#define fdio_root_handle (__fdio_global_state.root)
#define fdio_cwd_handle (__fdio_global_state.cwd)
#define fdio_cwd_lock (__fdio_global_state.cwd_lock)
#define fdio_cwd_path (__fdio_global_state.cwd_path)
#define fdio_fdtab (__fdio_global_state.fdtab)
#define fdio_root_init (__fdio_global_state.init)
#define fdio_root_ns (__fdio_global_state.ns)


// Enable low level debug chatter, which requires a kernel that
// doesn't check the resource argument to zx_debuglog_create()
//
// The value is the default debug level (0 = none)
// The environment variable FDIODEBUG will override this on fdio init
//
// #define FDIO_LLDEBUG 1

#ifdef FDIO_LLDEBUG
void fdio_lldebug_printf(unsigned level, const char* fmt, ...);
#define LOG(level, fmt...) fdio_lldebug_printf(level, fmt)
#else
#define LOG(level, fmt...) do {} while (0)
#endif

void fdio_set_debug_level(unsigned level);


// Enable intrusive allocation debugging
//
//#define FDIO_ALLOCDEBUG

static inline void fdio_acquire(fdio_t* io) {
    LOG(6, "fdio: acquire: %p\n", io);
    atomic_fetch_add(&io->refcount, 1);
}

static inline void fdio_release(fdio_t* io) {
    LOG(6, "fdio: release: %p\n", io);
    if (atomic_fetch_sub(&io->refcount, 1) == 1) {
        fdio_free(io);
    }
}

#ifdef FDIO_ALLOCDEBUG
void* fdio_alloc(size_t sz);
#else
static inline void* fdio_alloc(size_t sz) {
    void* ptr = calloc(1, sz);
    LOG(5, "fdio: io: alloc: %p\n", ptr);
    return ptr;
}
#endif
