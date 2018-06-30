// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <lib/fdio/limits.h>

#include <assert.h>
#include <limits.h>
#include <stdint.h>

__BEGIN_CDECLS

// clang-format off

// Determines whether or not client-side code should
// send requests using the Remote IO protocol or the
// FIDL protocol.
// #define ZXRIO_FIDL

#define ZXRIO_HDR_SZ       (__builtin_offsetof(zxrio_msg_t, data))

#define ZXRIO_ONE_HANDLE   0x00000100
#define ZXRIO_TWO_HANDLES  0x00000200

// Identifier that this is a FIDL message, not
// a ZXRIO message.
//
// The header is shared between the two formats.
#define ZXRIO_FIDL_ORD     0x80000000

// RIO Ordinals
#define ZXRIO_CLOSE        0x00000001
#define ZXRIO_CLONE       (0x00000002 | ZXRIO_ONE_HANDLE)
#define ZXRIO_OPEN        (0x00000003 | ZXRIO_ONE_HANDLE)
#define ZXRIO_MISC         0x00000004
#define ZXRIO_READ         0x00000005
#define ZXRIO_WRITE        0x00000006
#define ZXRIO_SEEK         0x00000007
#define ZXRIO_STAT         0x00000008
#define ZXRIO_READDIR      0x00000009
#define ZXRIO_IOCTL        0x0000000a
#define ZXRIO_IOCTL_1H    (0x0000000a | ZXRIO_ONE_HANDLE)
#define ZXRIO_IOCTL_2H    (0x0000000a | ZXRIO_TWO_HANDLES)
#define ZXRIO_UNLINK       0x0000000b
#define ZXRIO_READ_AT      0x0000000c
#define ZXRIO_WRITE_AT     0x0000000d
#define ZXRIO_TRUNCATE     0x0000000e
#define ZXRIO_RENAME      (0x0000000f | ZXRIO_ONE_HANDLE)
#define ZXRIO_CONNECT      0x00000010
#define ZXRIO_BIND         0x00000011
#define ZXRIO_LISTEN       0x00000012
#define ZXRIO_GETSOCKNAME  0x00000013
#define ZXRIO_GETPEERNAME  0x00000014
#define ZXRIO_GETSOCKOPT   0x00000015
#define ZXRIO_SETSOCKOPT   0x00000016
#define ZXRIO_GETADDRINFO  0x00000017
#define ZXRIO_SETATTR      0x00000018
#define ZXRIO_SYNC         0x00000019
#define ZXRIO_LINK        (0x0000001a | ZXRIO_ONE_HANDLE)
#define ZXRIO_MMAP         0x0000001b
#define ZXRIO_FCNTL        0x0000001c
#define ZXRIO_REWIND       0x0000001d
#define ZXRIO_NUM_OPS      30

// FIDL Ordinals

// Object
#define ZXFIDL_CLONE      0x80000001
#define ZXFIDL_CLOSE      0x80000002
#define ZXRIO_ON_OPEN     0x80000007

// Node
#define ZXFIDL_SYNC       0x81000001
#define ZXFIDL_STAT       0x81000002
#define ZXFIDL_SETATTR    0x81000003
#define ZXFIDL_IOCTL      0x81000004

// File
#define ZXFIDL_READ       0x82000001
#define ZXFIDL_READ_AT    0x82000002
#define ZXFIDL_WRITE      0x82000003
#define ZXFIDL_WRITE_AT   0x82000004
#define ZXFIDL_SEEK       0x82000005
#define ZXFIDL_TRUNCATE   0x82000006
#define ZXFIDL_GET_FLAGS  0x82000007
#define ZXFIDL_SET_FLAGS  0x82000008
#define ZXFIDL_GET_VMO    0x82000009

// Directory
#define ZXFIDL_OPEN       0x83000001
#define ZXFIDL_UNLINK     0x83000002
#define ZXFIDL_READDIR    0x83000003
#define ZXFIDL_REWIND     0x83000004
#define ZXFIDL_GET_TOKEN  0x83000005
#define ZXFIDL_RENAME     0x83000006
#define ZXFIDL_LINK       0x83000007

#define ZXRIO_FIDL_MSG(n)  ((n) & ZXRIO_FIDL_ORD)
#define ZXRIO_OP(n)        (n) // opcode
#define ZXRIO_HC(n)        (((n) >> 8) & 3) // handle count
#define ZXRIO_OPNAME(n)    ((n) & 0xFF) // opcode, "name" part only

#define ZXRIO_OPNAMES { \
    "status", "close", "clone", "open", \
    "misc", "read", "write", "seek", \
    "stat", "readdir", "ioctl", "unlink", \
    "read_at", "write_at", "truncate", "rename", \
    "connect", "bind", "listen", "getsockname", \
    "getpeername", "getsockopt", "setsockopt", "getaddrinfo", \
    "setattr", "sync", "link", "mmap", "fcntl", "rewind", }

// dispatcher callback return code that there were no messages to read
#define ERR_DISPATCHER_NO_WORK ZX_ERR_SHOULD_WAIT

// indicates message handed off to another server
// used by rio remote handler for deferred reply pipe completion
#define ERR_DISPATCHER_INDIRECT ZX_ERR_NEXT

// indicates the callback is taking responsibility for the
// channel receiving incoming messages.
//
// Unlike ERR_DISPATCHER_INDIRECT, this callback is propagated
// through the zxrio_handlers.
#define ERR_DISPATCHER_ASYNC ZX_ERR_ASYNC

// indicates that this was a close message and that no further
// callbacks should be made to the dispatcher
#define ERR_DISPATCHER_DONE ZX_ERR_STOP

const char* fdio_opname(uint32_t op);

typedef struct zxrio_msg zxrio_msg_t;

// callback to process a zxrio_msg
// - on entry datalen indicates how much valid data is in msg.data[]
// - return value of ERR_DISPATCHER_{INDIRECT,ASYNC} indicates that the reply is
//   being handled by the callback (forwarded to another server, sent later,
//   etc, and no reply message should be sent).
// - WARNING: Once this callback returns, usage of |msg| is no longer
//   valid. If a client transmits ERR_DISPATCHER_{INDIRECT,ASYNC}, and intends
//   to respond asynchronously, they must copy the fields of |msg| they
//   wish to use at a later point in time.
// - otherwise, the return value is treated as the status to send
//   in the rpc response, and msg.len indicates how much valid data
//   to send.  On error return msg.len will be set to 0.
typedef zx_status_t (*zxrio_cb_t)(zxrio_msg_t* msg, void* cookie);

// a fdio_dispatcher_handler suitable for use with a fdio_dispatcher
zx_status_t zxrio_handler(zx_handle_t h, zxrio_cb_t cb, void* cookie);

// the underlying handling for regular rpc or for a synthetic close,
// called by zxrio_handler.  handle_rpc() processes a single message
// from the provideded channel, returning a negative error value on
// error or 1 on clean shutdown (indicating no further callbacks
// should be made).  handle_close() processes a "synthetic" close
// event (eg, channel was remotely closed), and neither function
// should be callaed again after handle_close().
zx_status_t zxrio_handle_rpc(zx_handle_t h, zxrio_msg_t* msg, zxrio_cb_t cb, void* cookie);

// Invokes the callback with a "fake" close message. Useful when the
// client abruptly closes a handle without an explicit close message;
// this function allows the server to react the same way as a "clean" close.
zx_status_t zxrio_handle_close(zxrio_cb_t cb, void* cookie);

// Transmits a response message |msg| back to the client on the peer end of |h|.
zx_status_t zxrio_write_response(zx_handle_t h, zx_status_t status, zxrio_msg_t* msg);

// OPEN and CLOSE messages, can be forwarded to another remoteio server,
// without any need to wait for a reply.  The reply channel from the initial
// request is passed along to the new server.
// If the write to the server fails, an error reply is sent to the reply channel.
zx_status_t zxrio_txn_handoff(zx_handle_t server, zx_handle_t reply, zxrio_msg_t* msg);

// OPEN and CLONE ops do not return a reply
// Instead they receive a channel handle that they write their status
// and (if successful) type, extra data, and handles to.

static_assert(sizeof(zx_txid_t) == 4,
        "If the size of txid changes to 8 bytes then reserved0 should be removed from zxrio_msg");

typedef struct {
    uint32_t tag;
    uint32_t reserved;
    union {
        zx_handle_t handle;
        struct {
            zx_handle_t e;
        } file;
        struct {
            zx_handle_t s;
        } pipe;
        struct {
            zx_handle_t v;
            uint64_t offset;
            uint64_t length;
        } vmofile;
        struct {
            zx_handle_t e;
        } device;
        struct {
            zx_handle_t s;
        } socket;
    };
} zxrio_object_info_t;

#define ZXRIO_DESCRIBE_HDR_SZ       (__builtin_offsetof(zxrio_describe_t, extra))

// A one-way message which may be emitted by the server without an
// accompanying request. Optionally used as a part of the Open handshake.
typedef struct {
    zx_txid_t txid;                    // FIDL message header
    uint32_t reserved0;                // Padding
    uint32_t flags;
    uint32_t op;

    zx_status_t status;
    zxrio_object_info_t* extra_ptr;
    zxrio_object_info_t extra;
} zxrio_describe_t;

struct zxrio_msg {
    zx_txid_t txid;                    // FIDL message header
    uint32_t reserved0;
    uint32_t flags;
    uint32_t op;

    uint32_t datalen;                  // size of data[]
    int32_t arg;                       // tx: argument, rx: return value
    union {
        int64_t off;                   // tx/rx: offset where needed
        uint32_t mode;                 // tx: Open
        uint32_t op;                   // tx: Ioctl
    } arg2;
    int32_t reserved1;
    uint32_t hcount;                   // number of valid handles
    zx_handle_t handle[4];             // up to 3 handles + reply channel handle
    uint8_t data[FDIO_CHUNK_SIZE];     // payload
};

#define FDIO_MMAP_FLAG_READ    (1u << 0)
#define FDIO_MMAP_FLAG_WRITE   (1u << 1)
#define FDIO_MMAP_FLAG_EXEC    (1u << 2)
// Require a copy-on-write clone of the underlying VMO.
// The request should fail if the VMO is not cloned.
// May not be supplied with FDIO_MMAP_FLAG_EXACT.
#define FDIO_MMAP_FLAG_PRIVATE (1u << 16)
// Require an exact (non-cloned) handle to the underlying VMO.
// The request should fail if a handle to the exact VMO
// is not returned.
// May not be supplied with FDIO_MMAP_FLAG_PRIVATE.
#define FDIO_MMAP_FLAG_EXACT   (1u << 17)

static_assert(FDIO_MMAP_FLAG_READ == ZX_VM_FLAG_PERM_READ, "Vmar / Mmap flags should be aligned");
static_assert(FDIO_MMAP_FLAG_WRITE == ZX_VM_FLAG_PERM_WRITE, "Vmar / Mmap flags should be aligned");
static_assert(FDIO_MMAP_FLAG_EXEC == ZX_VM_FLAG_PERM_EXECUTE, "Vmar / Mmap flags should be aligned");

typedef struct zxrio_mmap_data {
    size_t offset;
    uint64_t length;
    int32_t flags;
} zxrio_mmap_data_t;

static_assert(FDIO_CHUNK_SIZE >= PATH_MAX, "FDIO_CHUNK_SIZE must be large enough to contain paths");

#define READDIR_CMD_NONE  0
#define READDIR_CMD_RESET 1

// - msg.datalen is the size of data sent or received and must be <= FDIO_CHUNK_SIZE
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
// on response arg32 is always zx_status, and may be positive for read/write calls

__END_CDECLS
