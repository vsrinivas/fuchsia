// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fdio/remoteio.h>
#include <fdio/vfs.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef struct zxrio zxrio_t;

// Atomically acquire a new txid
void zxrio_new_txid(zxrio_t* rio, zx_txid_t* txid);

// Acquire a rio object's RPC handle
zx_handle_t zxrio_handle(zxrio_t* rio);

// Encode and transmit an outgoing message (to a server)
zx_status_t zxrio_write_response(zx_handle_t h, zx_status_t status, zxrio_msg_t* msg);

// Read and decode an incoming message (from a client)
zx_status_t zxrio_read_request(zx_handle_t h, zxrio_msg_t* msg);

typedef struct fidl_open_response {
    alignas(FIDL_ALIGNMENT) ObjectOnOpenEvent response;
    alignas(FIDL_ALIGNMENT) ObjectInfo info;
} fidl_open_response_t;

// FIDL functions

// Request-only functions. These functions do not wait for a reply.
// |cnxn| is always consumed.
zx_status_t fidl_clone_request(zx_handle_t srv, zx_handle_t cnxn, uint32_t flags);
zx_status_t fidl_open_request(zx_handle_t srv, zx_handle_t cnxn, uint32_t flags,
                              uint32_t mode, const char* path, size_t pathlen);

// Request & response functions.
zx_status_t fidl_close(zxrio_t* rio);
zx_status_t fidl_write(zxrio_t* rio, const void* data, uint64_t length, uint64_t* actual);
zx_status_t fidl_writeat(zxrio_t* rio, const void* data, uint64_t length, off_t offset,
                         uint64_t* actual);
zx_status_t fidl_read(zxrio_t* rio, void* data, uint64_t length, uint64_t* actual);
zx_status_t fidl_readat(zxrio_t* rio, void* data, uint64_t length, off_t offset, uint64_t* actual);
zx_status_t fidl_seek(zxrio_t* rio, off_t offset, int whence, off_t* out);
zx_status_t fidl_stat(zxrio_t* rio, size_t len, vnattr_t* out, size_t* out_sz);
zx_status_t fidl_setattr(zxrio_t* rio, const vnattr_t* attr);
zx_status_t fidl_sync(zxrio_t* rio);
zx_status_t fidl_readdirents(zxrio_t* rio, void* data, size_t length, size_t* out_sz);
zx_status_t fidl_rewind(zxrio_t* rio);
zx_status_t fidl_unlink(zxrio_t* rio, const char* name, size_t namelen);
zx_status_t fidl_truncate(zxrio_t* rio, uint64_t length);
zx_status_t fidl_rename(zxrio_t* rio, const char* src, size_t srclen,
                        zx_handle_t dst_token, const char* dst, size_t dstlen);
zx_status_t fidl_link(zxrio_t* rio, const char* src, size_t srclen,
                      zx_handle_t dst_token, const char* dst, size_t dstlen);
zx_status_t fidl_ioctl(zxrio_t* rio, uint32_t op, const void* in_buf,
                       size_t in_len, void* out_buf, size_t out_len,
                       size_t* out_actual);
zx_status_t fidl_getvmo(zxrio_t* rio, uint32_t flags, zx_handle_t* out);
zx_status_t fidl_getflags(zxrio_t* rio, uint32_t* outflags);
zx_status_t fidl_setflags(zxrio_t* rio, uint32_t flags);

// Legacy RIO functions
bool is_rio_message_valid(zxrio_msg_t* msg);
bool is_rio_message_reply_valid(zxrio_msg_t* msg, uint32_t size);

__END_CDECLS
