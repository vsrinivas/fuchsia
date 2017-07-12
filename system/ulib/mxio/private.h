// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <mxio/limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <threads.h>

typedef struct mxio mxio_t;
typedef struct mxio_namespace mxio_ns_t;

// MXIO provides open/close/read/write io over various transports
// via the mxio_t interface abstraction.
//
// The PIPE protocol uses message ports as simple, no-flow-control
// io pipes with a maximum message size of MX_PIPE_SIZE.
//
// The REMOTEIO protocol uses message ports to implement simple
// synchronous remoting of read/write/close operations.
//
// The NULL protocol absorbs writes and is never readable.

typedef struct mxio_ops {
    ssize_t (*read)(mxio_t* io, void* data, size_t len);
    ssize_t (*read_at)(mxio_t* io, void* data, size_t len, off_t offset);
    ssize_t (*write)(mxio_t* io, const void* data, size_t len);
    ssize_t (*write_at)(mxio_t* io, const void* data, size_t len, off_t offset);
    ssize_t (*recvfrom)(mxio_t* io, void* data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen);
    ssize_t (*sendto)(mxio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen);
    ssize_t (*recvmsg)(mxio_t* io, struct msghdr* msg, int flags);
    ssize_t (*sendmsg)(mxio_t* io, const struct msghdr* msg, int flags);
    off_t (*seek)(mxio_t* io, off_t offset, int whence);
    mx_status_t (*misc)(mxio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* data, size_t len);
    mx_status_t (*close)(mxio_t* io);
    mx_status_t (*open)(mxio_t* io, const char* path, int32_t flags, uint32_t mode, mxio_t** out);
    mx_status_t (*clone)(mxio_t* io, mx_handle_t* out_handles, uint32_t* out_types);
    mx_status_t (*unwrap)(mxio_t* io, mx_handle_t* out_handles, uint32_t* out_types);
    mx_status_t (*shutdown)(mxio_t* io, int how);
    void (*wait_begin)(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* signals);
    void (*wait_end)(mxio_t* io, mx_signals_t signals, uint32_t* events);
    ssize_t (*ioctl)(mxio_t* io, uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);
    ssize_t (*posix_ioctl)(mxio_t* io, int req, va_list va);
    mx_status_t (*get_vmo)(mxio_t* io, mx_handle_t* out, size_t* off, size_t* len);
} mxio_ops_t;

// mxio_t flags
#define MXIO_FLAG_CLOEXEC ((int32_t)1 << 0)
#define MXIO_FLAG_SOCKET ((int32_t)1 << 1)
#define MXIO_FLAG_EPOLL ((int32_t)1 << 2)
#define MXIO_FLAG_WAITABLE ((int32_t)1 << 3)
#define MXIO_FLAG_SOCKET_CONNECTING ((int32_t)1 << 4)
#define MXIO_FLAG_SOCKET_CONNECTED ((int32_t)1 << 5)
#define MXIO_FLAG_NONBLOCK ((int32_t)1 << 6)

// The subset of mxio_t per-fd flags queryable via fcntl.
// Static assertions in unistd.c ensure we aren't colliding.
#define MXIO_FD_FLAGS MXIO_FLAG_CLOEXEC

typedef struct mxio {
    mxio_ops_t* ops;
    uint32_t magic;
    atomic_int_fast32_t refcount;
    int32_t dupcount;
    int32_t flags;
} mxio_t;

// Lifecycle notes:
//
// Upon creation, mxio objects have a refcount of 1.
// mxio_acquire() and mxio_release() are used to upref
// and downref, respectively.  Upon downref to 0,
// mxio_free() is called, which poisons the object and
// free()s it.
//
// The close hook must be called before free and should
// only be called once.  In normal use, mxio objects are
// accessed through the mxio_fdtab, and when close is
// called they are removed from the fdtab and the reference
// that the fdtab itself is holding is released, at which
// point they will be free()'d unless somebody is holding
// a ref due to an ongoing io transaction, which will
// certainly fail doe to underlying handles being closed
// at which point a downref will happen and destruction
// will follow.
//
// dupcount tracks how many fdtab entries an mxio object
// is in.  close() reduces the dupcount, and only actually
// closes the underlying object when it reaches zero.

#define MXIO_MAGIC 0x4f49584d // MXIO

static inline ssize_t mxio_read(mxio_t* io, void* data, size_t len) {
    return io->ops->read(io, data, len);
}
static inline ssize_t mxio_read_at(mxio_t* io, void* data, size_t len, off_t offset) {
    return io->ops->read_at(io, data, len, offset);
}
static inline ssize_t mxio_write(mxio_t* io, const void* data, size_t len) {
    return io->ops->write(io, data, len);
}
static inline ssize_t mxio_write_at(mxio_t* io, const void* data, size_t len, off_t offset) {
    return io->ops->write_at(io, data, len, offset);
}
static inline off_t mxio_seek(mxio_t* io, off_t offset, int whence) {
    return io->ops->seek(io, offset, whence);
}
static inline mx_status_t mxio_misc(mxio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* data, size_t len) {
    return io->ops->misc(io, op, off, maxreply, data, len);
}
static inline mx_status_t mxio_open(mxio_t* io, const char* path, int32_t flags, uint32_t mode, mxio_t** out) {
    return io->ops->open(io, path, flags, mode, out);
}
mx_status_t mxio_close(mxio_t* io);
mx_status_t mxio_wait(mxio_t* io, uint32_t events, mx_time_t deadline,
                      uint32_t* out_pending);

// Wraps a socket with an mxio_t using simple io.
// Takes ownership of h.
mxio_t* mxio_pipe_create(mx_handle_t h);

mx_status_t mxio_pipe_posix_ioctl(mxio_t* io, int req, va_list va);

// Wraps a vmo, offset, length with an mxio_t providing a readonly file.
// Takens ownership of h.
mxio_t* mxio_vmofile_create(mx_handle_t h, mx_off_t off, mx_off_t len);

// Wraps a socket with an mxio_t using socket io.
// Takes ownership of h and s.
mxio_t* mxio_socket_create(mx_handle_t h, mx_handle_t s, int flags);

// creates a message port and pair of simple io mxio_t's
int mxio_pipe_pair(mxio_t** a, mxio_t** b);

// create a mxio (if possible) from type, handles and extradata
mx_status_t mxio_from_handles(uint32_t type, mx_handle_t* handles, int hcount,
                              void* extra, uint32_t esize, mxio_t** out);

void mxio_free(mxio_t* io);

static inline void mxio_acquire(mxio_t* io) {
    atomic_fetch_add(&io->refcount, 1);
}

static inline void mxio_release(mxio_t* io) {
    if (atomic_fetch_sub(&io->refcount, 1) == 1) {
        mxio_free(io);
    }
}

mxio_t* mxio_ns_open_root(mxio_ns_t* ns);

// io will be consumed by this and must not be shared
void mxio_chdir(mxio_t* io, const char* path);

// Wraps an arbitrary handle with a mxio_t that works with wait hooks.
// Takes ownership of handle unless shared_handle is true.
mxio_t* mxio_waitable_create(mx_handle_t h, mx_signals_t signals_in, mx_signals_t signals_out, bool shared_handle);

void mxio_socket_set_stream_ops(mxio_t* io);
void mxio_socket_set_dgram_ops(mxio_t* io);

mx_status_t mxio_socket_posix_ioctl(mxio_t* io, int req, va_list va);
mx_status_t mxio_socket_shutdown(mxio_t* io, int how);
mx_status_t mxio_socketpair_shutdown(mxio_t* io, int how);

// unsupported / do-nothing hooks shared by implementations
ssize_t mxio_default_read(mxio_t* io, void* _data, size_t len);
ssize_t mxio_default_read_at(mxio_t* io, void* _data, size_t len, off_t offset);
ssize_t mxio_default_write(mxio_t* io, const void* _data, size_t len);
ssize_t mxio_default_write_at(mxio_t* io, const void* _data, size_t len, off_t offset);
ssize_t mxio_default_recvfrom(mxio_t* io, void* _data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen);
ssize_t mxio_default_sendto(mxio_t* io, const void* _data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen);
ssize_t mxio_default_recvmsg(mxio_t* io, struct msghdr* msg, int flags);
ssize_t mxio_default_sendmsg(mxio_t* io, const struct msghdr* msg, int flags);
off_t mxio_default_seek(mxio_t* io, off_t offset, int whence);
mx_status_t mxio_default_misc(mxio_t* io, uint32_t op, int64_t off, uint32_t arg, void* data, size_t len);
mx_status_t mxio_default_close(mxio_t* io);
mx_status_t mxio_default_open(mxio_t* io, const char* path, int32_t flags, uint32_t mode, mxio_t** out);
mx_status_t mxio_default_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types);
ssize_t mxio_default_ioctl(mxio_t* io, uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);
void mxio_default_wait_begin(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals);
void mxio_default_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events);
mx_status_t mxio_default_unwrap(mxio_t* io, mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_default_shutdown(mxio_t* io, int how);
ssize_t mxio_default_posix_ioctl(mxio_t* io, int req, va_list va);
mx_status_t mxio_default_get_vmo(mxio_t* io, mx_handle_t* out, size_t* off, size_t* len);

void __mxio_startup_handles_init(uint32_t num, mx_handle_t handles[],
                                 uint32_t handle_info[])
    __attribute__((visibility("hidden")));

void __mxio_rchannel_init(void) __attribute__((visibility("hidden")));

typedef struct {
    mtx_t lock;
    mtx_t cwd_lock;
    bool init;
    mode_t umask;
    mxio_t* root;
    mxio_t* cwd;
    mxio_t* fdtab[MAX_MXIO_FD];
    mxio_ns_t* ns;
    char cwd_path[PATH_MAX];
} mxio_state_t;

extern mxio_state_t __mxio_global_state;

#define mxio_lock (__mxio_global_state.lock)
#define mxio_root_handle (__mxio_global_state.root)
#define mxio_cwd_handle (__mxio_global_state.cwd)
#define mxio_cwd_lock (__mxio_global_state.cwd_lock)
#define mxio_cwd_path (__mxio_global_state.cwd_path)
#define mxio_fdtab (__mxio_global_state.fdtab)
#define mxio_root_init (__mxio_global_state.init)
#define mxio_root_ns (__mxio_global_state.ns)
