// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_STREAM_H_
#define LIB_ZX_STREAM_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/vmo.h>

namespace zx {

class stream final : public object<stream> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_STREAM;

  constexpr stream() = default;

  explicit stream(zx_handle_t value) : object(value) {}

  explicit stream(handle&& h) : object(h.release()) {}

  stream(stream&& other) : object(other.release()) {}

  stream& operator=(stream&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint32_t options, const vmo& vmo_handle, zx_off_t seek,
                            stream* out_stream);

  zx_status_t writev(uint32_t options, const zx_iovec_t* vector, size_t vector_count,
                     size_t* actual) const {
    return zx_stream_writev(get(), options, vector, vector_count, actual);
  }

  zx_status_t writev_at(uint32_t options, zx_off_t offset, const zx_iovec_t* vector,
                        size_t vector_count, size_t* actual) const {
    return zx_stream_writev_at(get(), options, offset, vector, vector_count, actual);
  }

  zx_status_t readv(uint32_t options, const zx_iovec_t* vector, size_t vector_count,
                    size_t* actual) const {
    // TODO: zx_stream_readv should accept a |const zx_iovec_t*|.
    return zx_stream_readv(get(), options, const_cast<zx_iovec_t*>(vector), vector_count, actual);
  }

  zx_status_t readv_at(uint32_t options, zx_off_t offset, const zx_iovec_t* vector,
                       size_t vector_count, size_t* actual) const {
    // TODO: zx_stream_readv should accept a |const zx_iovec_t*|.
    return zx_stream_readv_at(get(), options, offset, const_cast<zx_iovec_t*>(vector), vector_count,
                              actual);
  }

  zx_status_t seek(zx_stream_seek_origin_t whence, int64_t offset, zx_off_t* out_seek) const {
    return zx_stream_seek(get(), whence, offset, out_seek);
  }
};

using unowned_stream = unowned<stream>;

}  // namespace zx

#endif  // LIB_ZX_STREAM_H_
