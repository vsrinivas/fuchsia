// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_GUEST_H_
#define LIB_ZX_GUEST_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <zircon/availability.h>

namespace zx {

class guest final : public object<guest> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_GUEST;

  constexpr guest() = default;

  explicit guest(zx_handle_t value) : object(value) {}

  explicit guest(handle&& h) : object(h.release()) {}

  guest(guest&& other) : object(other.release()) {}

  guest& operator=(guest&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(const resource& resource, uint32_t options, guest* guest, vmar* vmar)
      ZX_AVAILABLE_SINCE(7);

  zx_status_t set_trap(uint32_t kind, zx_gpaddr_t addr, size_t len, const port& port,
                       uint64_t key) const ZX_AVAILABLE_SINCE(7) {
    return zx_guest_set_trap(get(), kind, addr, len, port.get(), key);
  }
} ZX_AVAILABLE_SINCE(7);

using unowned_guest = unowned<guest> ZX_AVAILABLE_SINCE(7);

}  // namespace zx

#endif  // LIB_ZX_GUEST_H_
