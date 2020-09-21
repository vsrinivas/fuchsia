// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_OPS_H_
#define LIB_ZXIO_OPS_H_

#include <lib/zxio/types.h>
#include <lib/zxio/zxio.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// See zxio.h
typedef struct zxio_tag {
  uint64_t reserved[4];
} zxio_t;

// Storage for the |zxio_ops_t| implementation.
// All |zxio_t| implementations must fit within this space.
typedef struct zxio_private {
  uint64_t reserved[6];
} zxio_private_t;

// See extensions.h
typedef struct zxio_extensions zxio_extensions_t;

// The storage backing a |zxio_t|.
typedef struct zxio_storage {
  zxio_t io;
  zxio_private_t reserved;
} zxio_storage_t;

// A table of operations for a zxio_t.
//
// Most of the functions that operate on a zxio_t call through this operations
// table to actually perform the operation. Use |zxio_init| to initialize a
// zxio_t with a custom operations table.
typedef struct zxio_ops {
  // Releases all resources held by |io|.
  // After |close| returns, any further ops must not be called relative to |io|.
  zx_status_t (*close)(zxio_t* io);

  // After |release| returns, any further ops most not be called relative to |io|,
  // except |close|.
  zx_status_t (*release)(zxio_t* io, zx_handle_t* out_handle);

  // TODO(tamird/abarth): clarify the semantics of this operation. fdio currently relies on this to
  // implement POSIX-style dup() which expects the seek pointer to be preserved, but zxio_vmo_clone
  // does not currently produce those semantics.
  zx_status_t (*clone)(zxio_t* io, zx_handle_t* out_handle);
  void (*wait_begin)(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                     zx_signals_t* out_zx_signals);
  void (*wait_end)(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals);
  zx_status_t (*sync)(zxio_t* io);
  zx_status_t (*attr_get)(zxio_t* io, zxio_node_attributes_t* out_attr);
  zx_status_t (*attr_set)(zxio_t* io, const zxio_node_attributes_t* attr);
  zx_status_t (*readv)(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                       zxio_flags_t flags, size_t* out_actual);
  zx_status_t (*readv_at)(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                          size_t vector_count, zxio_flags_t flags, size_t* out_actual);
  zx_status_t (*writev)(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                        zxio_flags_t flags, size_t* out_actual);
  zx_status_t (*writev_at)(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                           size_t vector_count, zxio_flags_t flags, size_t* out_actual);
  zx_status_t (*seek)(zxio_t* io, zxio_seek_origin_t start, int64_t offset, size_t* out_offset);
  zx_status_t (*truncate)(zxio_t* io, size_t length);
  zx_status_t (*flags_get)(zxio_t* io, uint32_t* out_flags);
  zx_status_t (*flags_set)(zxio_t* io, uint32_t flags);
  zx_status_t (*vmo_get)(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo, size_t* out_size);
  zx_status_t (*open)(zxio_t* io, uint32_t flags, uint32_t mode, const char* path, zxio_t** out_io);
  zx_status_t (*open_async)(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                            size_t path_len, zx_handle_t request);
  zx_status_t (*unlink)(zxio_t* io, const char* path);
  zx_status_t (*token_get)(zxio_t* io, zx_handle_t* out_token);
  zx_status_t (*rename)(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                        const char* dst_path);
  zx_status_t (*link)(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                      const char* dst_path);
  zx_status_t (*dirent_iterator_init)(zxio_t* io, zxio_dirent_iterator_t* iterator);
  zx_status_t (*dirent_iterator_next)(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                      zxio_dirent_t** out_entry);
  void (*dirent_iterator_destroy)(zxio_t* io, zxio_dirent_iterator_t* iterator);
  zx_status_t (*isatty)(zxio_t* io, bool* tty);
} zxio_ops_t;

// Initialize a |zxio_t| object with the given |ops| table.
void zxio_init(zxio_t* io, const zxio_ops_t* ops);

// Get the ops table used by the given |zxio_t| object.
const zxio_ops_t* zxio_get_ops(zxio_t* io);

__END_CDECLS

#endif  // LIB_ZXIO_OPS_H_
