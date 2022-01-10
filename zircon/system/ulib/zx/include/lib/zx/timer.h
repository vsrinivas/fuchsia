// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_TIMER_H_
#define LIB_ZX_TIMER_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <zircon/availability.h>
#include <zircon/types.h>

namespace zx {

class timer final : public object<timer> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_TIMER;

  constexpr timer() = default;

  explicit timer(zx_handle_t value) : object(value) {}

  explicit timer(handle&& h) : object(h.release()) {}

  timer(timer&& other) : object(other.release()) {}

  timer& operator=(timer&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint32_t options, zx_clock_t clock_id, timer* result)
      ZX_AVAILABLE_SINCE(7);

  zx_status_t set(zx::time deadline, zx::duration slack) const ZX_AVAILABLE_SINCE(7) {
    return zx_timer_set(get(), deadline.get(), slack.get());
  }

  zx_status_t cancel() const ZX_AVAILABLE_SINCE(7) { return zx_timer_cancel(get()); }
} ZX_AVAILABLE_SINCE(7);

using unowned_timer = unowned<timer> ZX_AVAILABLE_SINCE(7);

}  // namespace zx

#endif  // LIB_ZX_TIMER_H_
