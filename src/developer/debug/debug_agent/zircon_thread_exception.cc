// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_thread_exception.h"

namespace debug_agent {

fitx::result<zx_status_t, zx_koid_t> ZirconThreadException::GetThreadKoid() const {
  if (thread_koid_ == ZX_KOID_INVALID) {
    zx::thread thread;
    {
      zx_status_t status = exception_.get_thread(&thread);
      if (status != ZX_OK) {
        return fitx::error(status);
      }
    }
    zx_info_handle_basic_t info;
    {
      zx_status_t status =
          thread.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
      if (status != ZX_OK) {
        return fitx::error(status);
      }
    }
    thread_koid_ = info.koid;
  }
  return fitx::ok(thread_koid_);
}

fitx::result<zx_status_t, uint32_t> ZirconThreadException::GetState() const {
  uint32_t state = 0;
  zx_status_t status = exception_.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
  if (status != ZX_OK) {
    return fitx::error(status);
  }
  return fitx::ok(state);
}

fitx::result<zx_status_t, uint32_t> ZirconThreadException::GetStrategy() const {
  uint32_t strategy = 0;
  zx_status_t status =
      exception_.get_property(ZX_PROP_EXCEPTION_STRATEGY, &strategy, sizeof(strategy));
  if (status != ZX_OK) {
    return fitx::error(status);
  }
  return fitx::ok(strategy);
}

}  // namespace debug_agent
