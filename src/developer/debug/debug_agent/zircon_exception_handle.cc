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

fit::result<debug::Status, ExceptionHandle::Resolution> ZirconExceptionHandle::GetResolution()
    const {
  uint32_t state = 0;
  if (zx_status_t status = exception_.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
      status != ZX_OK)
    return fit::error(debug::ZxStatus(status));

  switch (state) {
    case ZX_EXCEPTION_STATE_TRY_NEXT:
      return fit::ok(Resolution::kTryNext);
    case ZX_EXCEPTION_STATE_HANDLED:
      return fit::ok(Resolution::kHandled);
  }
  FX_NOTREACHED();
  return fit::error(debug::ZxStatus(ZX_ERR_BAD_STATE));
}

debug::Status ZirconExceptionHandle::SetResolution(Resolution state) {
  uint32_t zx_state = 0;
  switch (state) {
    case Resolution::kTryNext:
      zx_state = ZX_EXCEPTION_STATE_TRY_NEXT;
      break;
    case Resolution::kHandled:
      zx_state = ZX_EXCEPTION_STATE_HANDLED;
      break;
  }
  return debug::ZxStatus(
      exception_.set_property(ZX_PROP_EXCEPTION_STATE, &zx_state, sizeof(state)));
}

fit::result<debug::Status, debug_ipc::ExceptionStrategy> ZirconExceptionHandle::GetStrategy()
    const {
  uint32_t raw_strategy = 0;
  zx_status_t status =
      exception_.get_property(ZX_PROP_EXCEPTION_STRATEGY, &raw_strategy, sizeof(raw_strategy));
  if (status != ZX_OK) {
    return fit::error(debug::ZxStatus(status));
  }
  auto strategy = debug_ipc::ToExceptionStrategy(raw_strategy);
  if (!strategy.has_value()) {
    return fit::error(debug::ZxStatus(ZX_ERR_BAD_STATE));
  }
  return fit::ok(strategy.value());
}

debug::Status ZirconExceptionHandle::SetStrategy(debug_ipc::ExceptionStrategy strategy) {
  auto raw_strategy = debug_ipc::ToRawValue(strategy);
  if (!raw_strategy.has_value()) {
    return debug::ZxStatus(ZX_ERR_BAD_STATE);
  }
  return debug::ZxStatus(exception_.set_property(ZX_PROP_EXCEPTION_STRATEGY, &raw_strategy.value(),
                                                 sizeof(raw_strategy.value())));
}

}  // namespace debug_agent
