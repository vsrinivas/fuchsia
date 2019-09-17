// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_INTERRUPT_H_
#define LIB_ZX_INTERRUPT_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <lib/zx/vcpu.h>

namespace zx {

class interrupt final : public object<interrupt> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_INTERRUPT;

  constexpr interrupt() = default;

  explicit interrupt(zx_handle_t value) : object(value) {}

  explicit interrupt(handle&& h) : object(h.release()) {}

  interrupt(interrupt&& other) : object(other.release()) {}

  interrupt& operator=(interrupt&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(const resource& resource, uint32_t vector, uint32_t options,
                            interrupt* result);

  zx_status_t wait(zx::time* timestamp) const {
    return zx_interrupt_wait(get(), timestamp ? timestamp->get_address() : nullptr);
  }

  zx_status_t destroy() const { return zx_interrupt_destroy(get()); }

  zx_status_t trigger(uint32_t options, zx::time timestamp) const {
    return zx_interrupt_trigger(get(), options, timestamp.get());
  }

  zx_status_t bind(const zx::port& port, uint64_t key, uint32_t options) const {
    return zx_interrupt_bind(get(), port.get(), key, options);
  }

  zx_status_t bind_vcpu(const zx::vcpu& vcpu, uint32_t options) const {
    return zx_interrupt_bind_vcpu(get(), vcpu.get(), options);
  }

  zx_status_t ack() const { return zx_interrupt_ack(get()); }
};

using unowned_interrupt = unowned<interrupt>;

}  // namespace zx

#endif  // LIB_ZX_INTERRUPT_H_
