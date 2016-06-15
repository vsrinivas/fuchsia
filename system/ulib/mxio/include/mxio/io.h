// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <magenta/types.h>
#include <sys/types.h>

// MXIO provides open/close/read/write io over various transports
// via the mxio_t interface abstraction.
//
// The PIPE protocol uses message ports as simple, no-flow-control
// io pipes with a maximum message size of MX_PIPE_SIZE.
//
// The REMOTEIO protocol uses message ports to implement simple
// synchronous remoting of read/write/close operations.
//
// Currently there is no multithreading support.  Do not use the
// same mxio_t endpoint from different threads.
//

typedef struct mxio mxio_t;
typedef struct mxio_ops mxio_ops_t;

struct mxio_ops {
    ssize_t (*read)(mxio_t* io, void* data, size_t len);
    ssize_t (*write)(mxio_t* io, const void* data, size_t len);
    off_t (*seek)(mxio_t* io, off_t offset, int whence);
    mx_status_t (*misc)(mxio_t* io, uint32_t op, uint32_t maxreply, void* data, size_t len);
    mx_status_t (*close)(mxio_t* io);
    mx_status_t (*open)(mxio_t* io, const char* path, int32_t flags, mxio_t** out);
    mx_status_t (*clone)(mxio_t* io, mx_handle_t* out_handles, uint32_t* out_types);
    mx_status_t (*wait)(mxio_t* io, uint32_t events, uint32_t* pending, mx_time_t timeout);
    ssize_t (*ioctl)(mxio_t* io, uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);
};

#define MXIO_EVT_READABLE MX_SIGNAL_USER0
#define MXIO_EVT_WRITABLE MX_SIGNAL_USER1
#define MXIO_EVT_ERROR MX_SIGNAL_USER2
#define MXIO_EVT_ALL (MXIO_EVT_READABLE | MXIO_EVT_WRITABLE | MXIO_EVT_ERROR)

struct mxio {
    mxio_ops_t* ops;
    uint32_t magic;
    uint32_t priv;
};

#define MXIO_MAGIC 0x4f49584d // MXIO

#define MXIO_NONBLOCKING 1

#define MXIO_PROTOCOL_UNDEFINED 0
#define MXIO_PROTOCOL_PIPE 1
#define MXIO_PROTOCOL_REMOTE 2

// maximum handles used in open/clone/create
#define MXIO_MAX_HANDLES 3

// mxio_ops_t's read/write are able to do io of
// at least this size
#define MXIO_CHUNK_SIZE 8192

// Maxium size for an ioctl input
#define MXIO_IOCTL_MAX_INPUT 1024

static inline ssize_t mx_read(mxio_t* io, void* data, size_t len) {
    return io->ops->read(io, data, len);
}
static inline ssize_t mx_write(mxio_t* io, const void* data, size_t len) {
    return io->ops->write(io, data, len);
}
static inline off_t mx_seek(mxio_t* io, off_t offset, int whence) {
    return io->ops->seek(io, offset, whence);
}
static inline mx_status_t mx_misc(mxio_t* io, uint32_t op, uint32_t maxreply, void* data, size_t len) {
    return io->ops->misc(io, op, maxreply, data, len);
}
static inline mx_status_t mx_close(mxio_t* io) {
    return io->ops->close(io);
}
static inline mx_status_t mx_open(mxio_t* io, const char* path, int32_t flags, mxio_t** out) {
    return io->ops->open(io, path, flags, out);
}

// creates a do-nothing mxio_t
mxio_t* mxio_null_create(void);

// wraps a message port with an mxio_t using simple io
mxio_t* mxio_pipe_create(mx_handle_t h);

// creates a mxio that wraps a log object
// this will allocate a per-thread buffer (on demand) to assemble
// entire log-lines and flush them on newline or buffer full.
mxio_t* mxio_logger_create(mx_handle_t);

// creates a message port and pair of simple io mxio_t's
int mxio_pipe_pair(mxio_t** a, mxio_t** b);

// wraps a message port with an mxio_t using remote io
mxio_t* mxio_remote_create(mx_handle_t h, mx_handle_t e);

// create a mxio (if possible) from type and handles
mx_status_t mxio_from_handles(uint32_t type, mx_handle_t* handles, int hcount, mxio_t** out);

// attempt to install a mxio in the unistd fd table
// if fd > 0, request a specific fd instead of first available
// returns fd on success
int mxio_bind_to_fd(mxio_t* io, int fd);

// wait until one or more events are pending
mx_status_t mxio_wait_fd(int fd, uint32_t events, uint32_t* pending);
