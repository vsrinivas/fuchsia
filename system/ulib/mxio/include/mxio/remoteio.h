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

#include <sys/types.h>
#include <magenta/types.h>
#include <mxio/io.h>

// clang-format off

#define MX_RIO_HDR_SZ       (__builtin_offsetof(mx_rio_msg_t, data))

#define MX_RIO_MAGIC        0x024F4952 // RIO 0x02

#define MX_RIO_STATUS       0x00000000
#define MX_RIO_CLOSE        0x00000001
#define MX_RIO_CLONE        0x00000002
#define MX_RIO_OPEN         0x00000003
#define MX_RIO_MISC         0x00000004
#define MX_RIO_READ         0x00000005
#define MX_RIO_WRITE        0x00000006
#define MX_RIO_SEEK         0x00000007
#define MX_RIO_STAT         0x00000008
#define MX_RIO_READDIR      0x00000009
#define MX_RIO_IOCTL        0x0000000a
#define MX_RIO_NUM_OPS      11

#define MX_RIO_OP(n)        ((n) & 0xFFFF)
#define MX_RIO_REPLY_PIPE   0x01000000

#define MX_RIO_OPNAMES { \
    "status", "close", "clone", "open", \
    "misc", "read", "write", "seek", \
    "stat", "readdir", "ioctl" }

typedef struct mx_rio_msg mx_rio_msg_t;

typedef mx_status_t (*mxio_rio_cb_t)(mx_rio_msg_t* msg, void* cookie);
// callback to process a mx_rio_msg
// - on entry datalen indicates how much valid data is in msg.data[]
// - return value will be placed in msg.arg, negative is an error,
//   positive values are opcode-specific
// - on non-error return msg.len indicates how much valid data to
//   send.  On error return msg.len will be set to 0.

// process events on h until it fails
void mxio_rio_server(mx_handle_t h, mxio_rio_cb_t cb, void* cookie);

// a mxio_dispatcher_handler suitable for use with a mxio_dispatcher
mx_status_t mxio_rio_handler(mx_handle_t h, void* cb, void* cookie);

// create a thread to service mxio remote io traffic
mx_status_t mxio_handler_create(mx_handle_t h, mxio_rio_cb_t cb, void* cookie);


struct mx_rio_msg {
    uint32_t magic;                    // MX_RIO_MAGIC
    uint32_t op;                       // opcode | flags
    uint32_t datalen;                  // size of data[]
    int32_t arg;                       // tx: argument, rx: return value
    int64_t off;                       // tx/rx: offset where needed
    int32_t reserved;
    uint32_t hcount;                   // number of valid handles
    mx_handle_t handle[4];             // up to 3 handles + reply pipe handle
    uint8_t data[MXIO_CHUNK_SIZE];     // payload
};

// - msg.datalen is the size of data sent or received and must be <= MXIO_CHUNK_SIZE
// - msg.arg is the return code on replies

// request------------------------------------   response------------------------------
// op        arg        off     data             off         data            handle[]
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
//
//
// proposed:
//
// LSTAT     maxreply   0       -                0           <vnattr_t>      -
// MKDIR     0          0       <name>           0           -               -
// READ_AT   maxread    offset  -                newoffset   <bytes>         -
// WRITE_AT  0          offset  <bytes>          newoffset   -               -
// UNLINK    0          0       <name>           0           -               -
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

