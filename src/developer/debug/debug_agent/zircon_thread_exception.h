// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_THREAD_EXCEPTION_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_THREAD_EXCEPTION_H_

#include <lib/zx/exception.h>
#include <lib/zx/process.h>

#include <utility>

#include "src/developer/debug/debug_agent/thread_exception.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

// Wraps a zx::exception, which is expected to be valid for the lifetime of
// an instance of this class.
class ZirconThreadException : public ThreadException {
 public:
  explicit ZirconThreadException(zx::exception exception) : exception_(std::move(exception)) {}

  ~ZirconThreadException() = default;

  fitx::result<zx_status_t, zx_koid_t> GetThreadKoid() const override;

  fitx::result<zx_status_t, uint32_t> GetState() const override;

  zx_status_t SetState(uint32_t state) override {
    return exception_.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
  }

  fitx::result<zx_status_t, uint32_t> GetStrategy() const override;

  zx_status_t SetStrategy(uint32_t strategy) override {
    return exception_.set_property(ZX_PROP_EXCEPTION_STRATEGY, &strategy, sizeof(strategy));
  }

 private:
  zx::exception exception_;
  // Default to an invalid koid, as that is the only canonical one.
  mutable zx_koid_t thread_koid_ = ZX_KOID_INVALID;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ZirconThreadException);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_THREAD_EXCEPTION_H_
