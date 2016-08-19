// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <mxio/io.h>
#include <stdint.h>
#include <system/compiler.h>

__BEGIN_CDECLS

// clang-format off

#define MXRIO_HDR_SZ       (__builtin_offsetof(mxrio_msg_t, data))

#define MXRIO_MAGIC        0x024F4952 // RIO 0x02

#define MXRIO_STATUS       0x00000000
#define MXRIO_CLOSE        0x00000001
#define MXRIO_CLONE        0x00000002
#define MXRIO_OPEN         0x00000003
#define MXRIO_MISC         0x00000004
#define MXRIO_READ         0x00000005
#define MXRIO_WRITE        0x00000006
#define MXRIO_SEEK         0x00000007
#define MXRIO_STAT         0x00000008
#define MXRIO_READDIR      0x00000009
#define MXRIO_IOCTL        0x0000000a
#define MXRIO_UNLINK       0x0000000b
#define MXRIO_NUM_OPS      12

#define MXRIO_OP(n)        ((n) & 0xFFFF)
#define MXRIO_REPLY_PIPE   0x01000000

#define MXRIO_OPNAMES { \
    "status", "close", "clone", "open", \
    "misc", "read", "write", "seek", \
    "stat", "readdir", "ioctl", "unlink" }

typedef struct mxrio_msg mxrio_msg_t;

typedef mx_status_t (*mxrio_cb_t)(mxrio_msg_t* msg, mx_handle_t rh, void* cookie);
// callback to process a mxrio_msg
// - on entry datalen indicates how much valid data is in msg.data[]
// - return value will be placed in msg.arg, negative is an error,
//   positive values are opcode-specific
// - on non-error return msg.len indicates how much valid data to
//   send.  On error return msg.len will be set to 0.
// - if rh is non-zero it is a reply handle which may be used for
//   deferred replies.  In which case the callback must return
//   ERR_DISPATCHER_INDIRECT to differentiate this from an immediate
//   reply or error

// a mxio_dispatcher_handler suitable for use with a mxio_dispatcher
mx_status_t mxrio_handler(mx_handle_t h, void* cb, void* cookie);

// Pass a message to another server (srv) along with a reply handle (rh)
// The other server will reply via the reply handle, and this call returns
// immediately, never blocking.
// The reply-handle is *not* closed on error, as it's expected the caller
// will want to send an error back via it.
mx_status_t mxrio_txn_handoff(mx_handle_t srv, mx_handle_t rh, mxrio_msg_t* msg);

struct mxrio_msg {
    uint32_t magic;                    // MXRIO_MAGIC
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
    mx_handle_t handle[4];             // up to 3 handles + reply pipe handle
    uint8_t data[MXIO_CHUNK_SIZE];     // payload
};

// - msg.datalen is the size of data sent or received and must be <= MXIO_CHUNK_SIZE
// - msg.arg is the return code on replies

// request------------------------------------   response------------------------------
// op        arg        arg2    data             arg2        data            handle[]
// --------- ---------- ------- --------------   ----------- --------------------------
// CLOSE     0          0       -                0           -               -
// CLONE     0          0       -                objtype     -               handle(s)
// OPEN      flags      mode    <name>           objtype     -               handle(s)
// READ      maxread    0       -                newoffset   <bytes>         -
// WRITE     0          0       <bytes>          newoffset   -               -
// SEEK      whence     offset  -                offset      -               -
// STAT      maxreply   0       -                0           <vnattr_t>      -
// READDIR   maxreply   0       -                0           <vndirent_t[]>  -
// IOCTL     out_len    opcode  <in_bytes>       0           <out_bytes>     -
// UNLINK    0          0       <name>           0           -               -
//
// proposed:
//
// LSTAT     maxreply   0       -                0           <vnattr_t>      -
// MKDIR     0          0       <name>           0           -               -
// READ_AT   maxread    offset  -                newoffset   <bytes>         -
// WRITE_AT  0          offset  <bytes>          newoffset   -               -
// RENAME*   name1len   0       <name1><name2>   0           -               -
// SYMLINK   namelen    0       <name><path>     0           -               -
// READLINK  maxreply   0       -                0           <path>          -
// MMAP      flags      offset  <uint64:len>     offset      -               vmohandle
// FLUSH     0          0       -                0           -               -
// SYNC      0          0       -                0           -               -
// LINK**    0          0       <name>           0           -               -
//
// on response arg32 is always mx_status, and may be positive for read/write calls
// * handle[0] used to pass reference to second directory handle
// ** handle[0] used to pass reference to target object

// allow for de-featuring this if it proves problematic
// TODO: make permanent if not
#define WITH_REPLY_PIPE 1

__END_CDECLS
