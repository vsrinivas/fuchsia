// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_FIFO_H_
#define LIB_ZX_FIFO_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

class fifo final : public object<fifo> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_FIFO;

  constexpr fifo() = default;

  explicit fifo(zx_handle_t value) : object(value) {}

  explicit fifo(handle&& h) : object(h.release()) {}

  fifo(fifo&& other) : object(other.release()) {}

  fifo& operator=(fifo&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint32_t elem_count, uint32_t elem_size, uint32_t options, fifo* out0,
                            fifo* out1);

  zx_status_t write(size_t elem_size, const void* buffer, size_t count,
                    size_t* actual_count) const {
    return zx_fifo_write(get(), elem_size, buffer, count, actual_count);
  }

  zx_status_t read(size_t elem_size, void* buffer, size_t count, size_t* actual_count) const {
    return zx_fifo_read(get(), elem_size, buffer, count, actual_count);
  }
};

using unowned_fifo = unowned<fifo>;

}  // namespace zx

#endif  // LIB_ZX_FIFO_H_
