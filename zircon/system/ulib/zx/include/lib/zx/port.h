// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_PORT_H_
#define LIB_ZX_PORT_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/time.h>
#include <zircon/availability.h>

namespace zx {

class port final : public object<port> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_PORT;

  constexpr port() = default;

  explicit port(zx_handle_t value) : object(value) {}

  explicit port(handle&& h) : object(h.release()) {}

  port(port&& other) : object(other.release()) {}

  port& operator=(port&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint32_t options, port* result) ZX_AVAILABLE_SINCE(7);

  zx_status_t queue(const zx_port_packet_t* packet) const ZX_AVAILABLE_SINCE(7) {
    return zx_port_queue(get(), packet);
  }

  zx_status_t wait(zx::time deadline, zx_port_packet_t* packet) const ZX_AVAILABLE_SINCE(7) {
    return zx_port_wait(get(), deadline.get(), packet);
  }

  zx_status_t cancel(const object_base& source, uint64_t key) const ZX_AVAILABLE_SINCE(7) {
    return zx_port_cancel(get(), source.get(), key);
  }
} ZX_AVAILABLE_SINCE(7);

using unowned_port = unowned<port> ZX_AVAILABLE_SINCE(7);

}  // namespace zx

#endif  // LIB_ZX_PORT_H_
