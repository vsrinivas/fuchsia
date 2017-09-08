// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

#include <mxio/limits.h>

#include <assert.h>
#include <limits.h>
#include <stdint.h>

__BEGIN_CDECLS

// clang-format off

#define MXRIO_HDR_SZ       (__builtin_offsetof(mxrio_msg_t, data))

#define MXRIO_ONE_HANDLE   0x00000100

#define MXRIO_STATUS       0x00000000
#define MXRIO_CLOSE        0x00000001
#define MXRIO_CLONE       (0x00000002 | MXRIO_ONE_HANDLE)
#define MXRIO_OPEN        (0x00000003 | MXRIO_ONE_HANDLE)
#define MXRIO_MISC         0x00000004
#define MXRIO_READ         0x00000005
#define MXRIO_WRITE        0x00000006
#define MXRIO_SEEK         0x00000007
#define MXRIO_STAT         0x00000008
#define MXRIO_READDIR      0x00000009
#define MXRIO_IOCTL        0x0000000a
#define MXRIO_IOCTL_1H    (0x0000000a | MXRIO_ONE_HANDLE)
#define MXRIO_UNLINK       0x0000000b
#define MXRIO_READ_AT      0x0000000c
#define MXRIO_WRITE_AT     0x0000000d
#define MXRIO_TRUNCATE     0x0000000e
#define MXRIO_RENAME      (0x0000000f | MXRIO_ONE_HANDLE)
#define MXRIO_CONNECT      0x00000010
#define MXRIO_BIND         0x00000011
#define MXRIO_LISTEN       0x00000012
#define MXRIO_GETSOCKNAME  0x00000013
#define MXRIO_GETPEERNAME  0x00000014
#define MXRIO_GETSOCKOPT   0x00000015
#define MXRIO_SETSOCKOPT   0x00000016
#define MXRIO_GETADDRINFO  0x00000017
#define MXRIO_SETATTR      0x00000018
#define MXRIO_SYNC         0x00000019
#define MXRIO_LINK        (0x0000001a | MXRIO_ONE_HANDLE)
#define MXRIO_MMAP         0x0000001b
#define MXRIO_FCNTL        0x0000001c
#define MXRIO_NUM_OPS      29

#define MXRIO_OP(n)        ((n) & 0x3FF) // opcode
#define MXRIO_HC(n)        (((n) >> 8) & 3) // handle count
#define MXRIO_OPNAME(n)    ((n) & 0xFF) // opcode, "name" part only

#define MXRIO_OPNAMES { \
    "status", "close", "clone", "open", \
    "misc", "read", "write", "seek", \
    "stat", "readdir", "ioctl", "unlink", \
    "read_at", "write_at", "truncate", "rename", \
    "connect", "bind", "listen", "getsockname", \
    "getpeername", "getsockopt", "setsockopt", "getaddrinfo", \
    "setattr", "sync", "link", "mmap", "fcntl" }

// dispatcher callback return code that there were no messages to read
#define ERR_DISPATCHER_NO_WORK MX_ERR_SHOULD_WAIT

// indicates message handed off to another server
// used by rio remote handler for deferred reply pipe completion
#define ERR_DISPATCHER_INDIRECT MX_ERR_NEXT

// indicates that this was a close message and that no further
// callbacks should be made to the dispatcher
#define ERR_DISPATCHER_DONE MX_ERR_STOP

const char* mxio_opname(uint32_t op);

typedef struct mxrio_msg mxrio_msg_t;

typedef mx_status_t (*mxrio_cb_t)(mxrio_msg_t* msg, void* cookie);
// callback to process a mxrio_msg
// - on entry datalen indicates how much valid data is in msg.data[]
// - return value of ERR_DISPATCHER_INDIRECT indicates that the
//   reply is being handled by the callback (forwarded to another
//   server, sent later, etc, and no reply message should be sent.
// - otherwise, the return value is treated as the status to send
//   in the rpc response, and msg.len indicates how much valid data
//   to send.  On error return msg.len will be set to 0.

// a mxio_dispatcher_handler suitable for use with a mxio_dispatcher
mx_status_t mxrio_handler(mx_handle_t h, void* cb, void* cookie);

// the underlying handling for regular rpc or for a synthetic close,
// called by mxrio_handler.  handle_rpc() processes a single message
// from the provideded channel, returning a negative error value on
// error or 1 on clean shutdown (indicating no further callbacks
// should be made).  handle_close() processes a "synthetic" close
// event (eg, channel was remotely closed), and neither function
// should be callaed again after handle_close().
mx_status_t mxrio_handle_rpc(mx_handle_t h, mxrio_msg_t* msg, mxrio_cb_t cb, void* cookie);
mx_status_t mxrio_handle_close(mxrio_cb_t cb, void* cookie);

// OPEN and CLOSE messages, can be forwarded to another remoteio server,
// without any need to wait for a reply.  The reply channel from the initial
// request is passed along to the new server.
// If the write to the server fails, an error reply is sent to the reply channel.
void mxrio_txn_handoff(mx_handle_t server, mx_handle_t reply, mxrio_msg_t* msg);


// OPEN and CLONE ops do not return a reply
// Instead they receive a channel handle that they write their status
// and (if successful) type, extra data, and handles to.

#define MXRIO_OBJECT_EXTRA 32
#define MXRIO_OBJECT_MINSIZE (2 * sizeof(uint32_t))
#define MXRIO_OBJECT_MAXSIZE (MXRIO_OBJECT_MINSIZE + MXRIO_OBJECT_EXTRA)

typedef struct {
    // Required Header
    mx_status_t status;
    uint32_t type;

    // Optional Extra Data
    uint8_t extra[MXRIO_OBJECT_EXTRA];

    // OOB Data
    uint32_t esize;
    uint32_t hcount;
    mx_handle_t handle[MXIO_MAX_HANDLES];
} mxrio_object_t;


struct mxrio_msg {
    mx_txid_t txid;                    // transaction id
    uint32_t op;                       // opcode
    uint32_t datalen;                  // size of data[]
    int32_t arg;                       // tx: argument, rx: return value
    union {
        int64_t off;                   // tx/rx: offset where needed
        uint32_t mode;                 // tx: Open
        uint32_t protocol;             // rx: Open
        uint32_t op;                   // tx: Ioctl
    } arg2;
    int32_t reserved;
    uint32_t hcount;                   // number of valid handles
    mx_handle_t handle[4];             // up to 3 handles + reply channel handle
    uint8_t data[MXIO_CHUNK_SIZE];     // payload
};

#define MXIO_MMAP_FLAG_READ    (1u << 0)
#define MXIO_MMAP_FLAG_WRITE   (1u << 1)
#define MXIO_MMAP_FLAG_EXEC    (1u << 2)
#define MXIO_MMAP_FLAG_PRIVATE (1u << 16)

static_assert(MXIO_MMAP_FLAG_READ == MX_VM_FLAG_PERM_READ, "Vmar / Mmap flags should be aligned");
static_assert(MXIO_MMAP_FLAG_WRITE == MX_VM_FLAG_PERM_WRITE, "Vmar / Mmap flags should be aligned");
static_assert(MXIO_MMAP_FLAG_EXEC == MX_VM_FLAG_PERM_EXECUTE, "Vmar / Mmap flags should be aligned");

typedef struct mxrio_mmap_data {
    size_t offset;
    uint64_t length;
    int32_t flags;
} mxrio_mmap_data_t;

static_assert(MXIO_CHUNK_SIZE >= PATH_MAX, "MXIO_CHUNK_SIZE must be large enough to contain paths");

#define READDIR_CMD_NONE  0
#define READDIR_CMD_RESET 1

// - msg.datalen is the size of data sent or received and must be <= MXIO_CHUNK_SIZE
// - msg.arg is the return code on replies

// request---------------------------------------    response------------------------------
// op          arg        arg2     data              arg2        data            handle[]
// ----------- ---------- -------  --------------    ----------- --------------------------
// CLOSE       0          0        -                 0           -               -
// CLONE       0          0        -                 objtype     -               handle(s)
// OPEN        flags      mode     <name>            objtype     -               handle(s)
// READ        maxread    0        -                 newoffset   <bytes>         -
// READ_AT     maxread    offset   -                 0           <bytes>         -
// WRITE       0          0        <bytes>           newoffset   -               -
// WRITE_AT    0          offset   <bytes>           0           -               -
// SEEK        whence     offset   -                 offset      -               -
// STAT        maxreply   0        -                 0           <vnattr_t>      -
// READDIR     maxreply   cmd      -                 0           <vndirent_t[]>  -
// IOCTL       out_len    opcode   <in_bytes>        0           <out_bytes>     -
// UNLINK      0          0        <name>            0           -               -
// TRUNCATE    0          offset   -                 0           -               -
// RENAME      0          0        <name1>0<name2>0  0           -               -
// CONNECT     0          0        <sockaddr>        0           -               -
// BIND        0          0        <sockaddr>        0           -               -
// LISTEN      0          0        <backlog>         0           -               -
// GETSOCKNAME maxreply   0        -                 0           <sockaddr>      -
// GETPEERNAME maxreply   0        -                 0           <sockaddr>      -
// GETSOCKOPT  maxreply   0        <sockopt>         0           <sockopt>       -
// SETSOCKOPT  0          0        <sockopt>         0           <sockopt>       -
// GETADDRINFO maxreply   0        <getaddrinfo>     0           <getaddrinfo>   -
// SETATTR     0          0        <vnattr>          0           -               -
// SYNC        0          0        0                 0           -               -
// LINK        0          0        <name1>0<name2>0  0           -               -
// MMAP        maxreply   0        mmap_data_msg     0           mmap_data_msg   vmohandle
// FCNTL       cmd        flags    0                 flags       -               -
//
// proposed:
//
// LSTAT       maxreply   0        -                 0           <vnattr_t>      -
// MKDIR       0          0        <name>            0           -               -
// SYMLINK     namelen    0        <name><path>      0           -               -
// READLINK    maxreply   0        -                 0           <path>          -
// FLUSH       0          0        -                 0           -               -
//
// on response arg32 is always mx_status, and may be positive for read/write calls

__END_CDECLS
