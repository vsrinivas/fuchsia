// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>

__BEGIN_CDECLS

typedef struct zxrio zxrio_t;

// Acquire a rio object's RPC handle
zx_handle_t zxrio_handle(zxrio_t* rio);

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
zx_status_t fidl_stat(zxrio_t* rio, vnattr_t* out);
zx_status_t fidl_setattr(zxrio_t* rio, const vnattr_t* attr);
zx_status_t fidl_sync(zxrio_t* rio);
zx_status_t fidl_readdirents(zxrio_t* rio, void* data, size_t length, size_t* out_sz);
zx_status_t fidl_rewind(zxrio_t* rio);
zx_status_t fidl_gettoken(zxrio_t* rio, zx_handle_t* out);
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

__END_CDECLS
