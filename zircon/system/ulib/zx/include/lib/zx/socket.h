// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_SOCKET_H_
#define LIB_ZX_SOCKET_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <zircon/availability.h>

namespace zx {

class socket final : public object<socket> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_SOCKET;

  constexpr socket() = default;

  explicit socket(zx_handle_t value) : object(value) {}

  explicit socket(handle&& h) : object(h.release()) {}

  socket(socket&& other) : object(other.release()) {}

  socket& operator=(socket&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint32_t options, socket* endpoint0, socket* endpoint1)
      ZX_AVAILABLE_SINCE(7);

  zx_status_t write(uint32_t options, const void* buffer, size_t len, size_t* actual) const
      ZX_AVAILABLE_SINCE(7) {
    return zx_socket_write(get(), options, buffer, len, actual);
  }

  zx_status_t read(uint32_t options, void* buffer, size_t len, size_t* actual) const
      ZX_AVAILABLE_SINCE(7) {
    return zx_socket_read(get(), options, buffer, len, actual);
  }

  zx_status_t set_disposition(uint32_t disposition, uint32_t disposition_peer) const
      ZX_AVAILABLE_SINCE(7) {
    return zx_socket_set_disposition(get(), disposition, disposition_peer);
  }
} ZX_AVAILABLE_SINCE(7);

using unowned_socket = unowned<socket> ZX_AVAILABLE_SINCE(7);

}  // namespace zx

#endif  // LIB_ZX_SOCKET_H_
