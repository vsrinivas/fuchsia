// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_SUSPEND_TOKEN_H_
#define LIB_ZX_SUSPEND_TOKEN_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

// The only thing you can do with a suspend token is close it (which will
// resume the thread).
class suspend_token final : public object<suspend_token> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_SUSPEND_TOKEN;

  constexpr suspend_token() = default;

  explicit suspend_token(zx_handle_t value) : object<suspend_token>(value) {}

  explicit suspend_token(handle&& h) : object<suspend_token>(h.release()) {}

  suspend_token(suspend_token&& other) : object<suspend_token>(other.release()) {}

  suspend_token& operator=(suspend_token&& other) {
    reset(other.release());
    return *this;
  }
};

}  // namespace zx

#endif  // LIB_ZX_SUSPEND_TOKEN_H_
