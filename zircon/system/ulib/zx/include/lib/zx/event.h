// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_EVENT_H_
#define LIB_ZX_EVENT_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

class event final : public object<event> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_EVENT;

  constexpr event() = default;

  explicit event(zx_handle_t value) : object(value) {}

  explicit event(handle&& h) : object(h.release()) {}

  event(event&& other) : object(other.release()) {}

  event& operator=(event&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint32_t options, event* result);
};

using unowned_event = unowned<event>;

}  // namespace zx

#endif  // LIB_ZX_EVENT_H_
