// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_VCPU_H_
#define LIB_ZX_VCPU_H_

#include <lib/zx/guest.h>
#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <zircon/availability.h>
#include <zircon/syscalls/port.h>

namespace zx {

class vcpu final : public object<vcpu> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_VCPU;

  constexpr vcpu() = default;

  explicit vcpu(zx_handle_t value) : object(value) {}

  explicit vcpu(handle&& h) : object(h.release()) {}

  vcpu(vcpu&& other) : object(other.release()) {}

  vcpu& operator=(vcpu&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(const guest& guest, uint32_t options, zx_gpaddr_t entry, vcpu* result)
      ZX_AVAILABLE_SINCE(7);

  zx_status_t enter(zx_port_packet_t* packet) const ZX_AVAILABLE_SINCE(7) {
    return zx_vcpu_enter(get(), packet);
  }

  zx_status_t kick() const ZX_AVAILABLE_SINCE(7) { return zx_vcpu_kick(get()); }

  zx_status_t interrupt(uint32_t interrupt) const ZX_AVAILABLE_SINCE(7) {
    return zx_vcpu_interrupt(get(), interrupt);
  }

  zx_status_t read_state(uint32_t kind, void* buf, size_t len) const ZX_AVAILABLE_SINCE(7) {
    return zx_vcpu_read_state(get(), kind, buf, len);
  }

  zx_status_t write_state(uint32_t kind, const void* buf, size_t len) const ZX_AVAILABLE_SINCE(7) {
    return zx_vcpu_write_state(get(), kind, buf, len);
  }
} ZX_AVAILABLE_SINCE(7);

using unowned_vcpu = unowned<vcpu> ZX_AVAILABLE_SINCE(7);

}  // namespace zx

#endif  // LIB_ZX_VCPU_H_
