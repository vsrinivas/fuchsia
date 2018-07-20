// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <lib/fdio/limits.h>

#include <assert.h>
#include <limits.h>
#include <stdint.h>

__BEGIN_CDECLS

// clang-format off

// FIDL Ordinals

// Object
#define ZXFIDL_CLONE      0x80000001
#define ZXFIDL_CLOSE      0x80000002
#define ZXFIDL_ON_OPEN    0x80000007

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

// Fuchsia-io limits.
//
// TODO(FIDL-127): Compute these values with the "union of all fuchsia-io"
// messages.
#define ZXFIDL_MAX_MSG_BYTES    (FDIO_CHUNK_SIZE * 2)
#define ZXFIDL_MAX_MSG_HANDLES  (FDIO_MAX_HANDLES)

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

// callback to process a FIDL message.
// - |msg| is a decoded FIDL message.
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
typedef zx_status_t (*zxrio_cb_t)(fidl_msg_t* msg, void* cookie);

// a fdio_dispatcher_handler suitable for use with a fdio_dispatcher
zx_status_t zxrio_handler(zx_handle_t h, zxrio_cb_t cb, void* cookie);

// the underlying handling for regular rpc or for a synthetic close,
// called by zxrio_handler.  handle_rpc() processes a single message
// from the provideded channel, returning a negative error value on
// error or 1 on clean shutdown (indicating no further callbacks
// should be made).  handle_close() processes a "synthetic" close
// event (eg, channel was remotely closed), and neither function
// should be called again after handle_close().
zx_status_t zxrio_handle_rpc(zx_handle_t h, zxrio_cb_t cb, void* cookie);

// Invokes the callback with a "fake" close message. Useful when the
// client abruptly closes a handle without an explicit close message;
// this function allows the server to react the same way as a "clean" close.
zx_status_t zxrio_handle_close(zxrio_cb_t cb, void* cookie);

// Transmits a response message |msg| back to the client on the peer end of |h|.
zx_status_t zxrio_write_response(zx_handle_t h, zx_status_t status, fidl_msg_t* msg);

// OPEN and CLONE ops do not return a reply
// Instead they receive a channel handle that they write their status
// and (if successful) type, extra data, and handles to.

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

__END_CDECLS
