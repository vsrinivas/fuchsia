// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_OPS_H_
#define LIB_ZXIO_OPS_H_

#include <lib/zxio/zxio.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// A table of operations for a zxio_t.
//
// Most of the functions that operate on a zxio_t call through this operations
// table to actually perform the operation. Use |zxio_alloc| to create a zxio_t
// with a custom operations table.
typedef struct zxio_ops zxio_ops_t;

// Allocate a |zxio_t| object with the given |ops| table.
//
// The allocated |zxio_t| will have an embedded |ctx| object of at least
// |ctx_size|. When this function returns, the |ctx| object will have been
// initialized to zero.
//
// The memory allocated by this function is freed by calling either
// |zxio_release| or |zxio_close|.
zx_status_t zxio_alloc(const zxio_ops_t* ops, size_t ctx_size, zxio_t** out_io);

// Returns a pointer to the |ctx| object embedded in the given |io|.
//
// The |ctx| object is private storage for use by the caller of |zxio_alloc|
// and should not be accessed or modified by other clients.
//
// The |ctx| object is guaranteed to have 16 byte alignment.
static inline void* zxio_ctx_get(zxio_t* io) {
    return ((char*)io) + 4 * sizeof(uint64_t);
}

struct zxio_ops {
    // After |release| returns, no further ops will be called relative to |ctx|.
    zx_status_t (*release)(void* ctx, zx_handle_t* out_node);

    // After |close| returns, no further ops will be called relative to |ctx|.
    zx_status_t (*close)(void* ctx);

    zx_status_t (*clone_async)(void* ctx, uint32_t flags, zx_handle_t request);
    zx_status_t (*sync)(void* ctx);
    zx_status_t (*attr_get)(void* ctx, zxio_node_attr_t* out_attr);
    zx_status_t (*attr_set)(void* ctx, uint32_t flags,
                            const zxio_node_attr_t* attr);
    zx_status_t (*read)(void* ctx, void* buffer, size_t capacity,
                        size_t* out_actual);
    zx_status_t (*read_at)(void* ctx, size_t offset, void* buffer,
                           size_t capacity, size_t* out_actual);
    zx_status_t (*write)(void* ctx, const void* buffer, size_t capacity,
                         size_t* out_actual);
    zx_status_t (*write_at)(void* ctx, size_t offset, const void* buffer,
                            size_t capacity, size_t* out_actual);
    zx_status_t (*seek)(void* ctx, size_t offset, zxio_seek_origin_t start,
                        size_t* out_offset);
    zx_status_t (*trucate)(void* ctx, size_t length);
    zx_status_t (*flags_get)(void* ctx, uint32_t* out_flags);
    zx_status_t (*flags_set)(void* ctx, uint32_t flags);
    zx_status_t (*vmo_get)(void* ctx, uint32_t flags, zx_handle_t* out_vmo,
                           size_t* out_size);
    zx_status_t (*open)(void* ctx, uint32_t flags, uint32_t mode,
                        const char* path, zxio_t** out_io);
    zx_status_t (*open_async)(void* ctx, uint32_t flags, uint32_t mode,
                              const char* path, zx_handle_t request);
    zx_status_t (*unlink)(void* ctx, const char* path);
    zx_status_t (*token_get)(void* ctx, zx_handle_t* out_token);
    zx_status_t (*rename)(void* ctx, const char* src_path,
                          zx_handle_t dst_token, const char* dst_path);
    zx_status_t (*link)(void* ctx, const char* src_path, zx_handle_t dst_token,
                        const char* dst_path);
    zx_status_t (*readdir)(void* ctx, void* buffer, size_t capacity,
                           size_t* out_actual);
    zx_status_t (*rewind)(void* ctx);
};

__END_CDECLS

#endif // LIB_ZXIO_OPS_H_
