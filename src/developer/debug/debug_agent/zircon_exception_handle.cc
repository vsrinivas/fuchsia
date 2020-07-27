// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_exception_handle.h"

#include "src/developer/debug/debug_agent/zircon_thread_handle.h"

namespace debug_agent {

std::unique_ptr<ThreadHandle> ZirconExceptionHandle::GetThreadHandle() const {
  zx::thread thread;
  if (zx_status_t status = exception_.get_thread(&thread); status != ZX_OK)
    return nullptr;
  return std::make_unique<ZirconThreadHandle>(std::move(thread));
}

debug_ipc::ExceptionType ZirconExceptionHandle::GetType(const ThreadHandle& thread) const {
  return arch::DecodeExceptionType(thread.GetNativeHandle(), info_.type);
}

fitx::result<zx_status_t, uint32_t> ZirconExceptionHandle::GetState() const {
  uint32_t state = 0;
  zx_status_t status = exception_.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
  if (status != ZX_OK) {
    return fitx::error(status);
  }
  return fitx::ok(state);
}

zx_status_t ZirconExceptionHandle::SetState(uint32_t state) {
  return exception_.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
}

fitx::result<zx_status_t, debug_ipc::ExceptionStrategy> ZirconExceptionHandle::GetStrategy() const {
  uint32_t raw_strategy = 0;
  zx_status_t status =
      exception_.get_property(ZX_PROP_EXCEPTION_STRATEGY, &raw_strategy, sizeof(raw_strategy));
  if (status != ZX_OK) {
    return fitx::error(status);
  }
  auto strategy = debug_ipc::ToExceptionStrategy(raw_strategy);
  if (!strategy.has_value()) {
    return fitx::error(ZX_ERR_BAD_STATE);
  }
  return fitx::ok(strategy.value());
}

zx_status_t ZirconExceptionHandle::SetStrategy(debug_ipc::ExceptionStrategy strategy) {
  auto raw_strategy = debug_ipc::ToRawValue(strategy);
  if (!raw_strategy.has_value()) {
    return ZX_ERR_BAD_STATE;
  }
  return exception_.set_property(ZX_PROP_EXCEPTION_STRATEGY, &raw_strategy.value(),
                                 sizeof(raw_strategy.value()));
}

}  // namespace debug_agent
