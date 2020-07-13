
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_EXCEPTION_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_EXCEPTION_H_

#include <zircon/syscalls/exception.h>

#include <utility>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/developer/debug/debug_agent/thread_exception.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

// ThreadException abstracts zx::exception, allowing for a more straightforward
// implementation in tests in overrides of this class.
class MockThreadException : public ThreadException {
 public:
  MockThreadException() = default;
  explicit MockThreadException(uint64_t thread_koid) : thread_koid_(thread_koid) {}
  ~MockThreadException() = default;

  fitx::result<zx_status_t, zx_koid_t> GetThreadKoid() const override {
    return fitx::ok(thread_koid_);
  }

  fitx::result<zx_status_t, uint32_t> GetState() const override { return fitx::ok(state_); }

  zx_status_t SetState(uint32_t state) override {
    state_ = state;
    return ZX_OK;
  }

  fitx::result<zx_status_t, uint32_t> GetStrategy() const override { return fitx::ok(strategy_); }

  zx_status_t SetStrategy(uint32_t strategy) override {
    strategy_ = strategy;
    return ZX_OK;
  }

 private:
  uint64_t thread_koid_ = ZX_KOID_INVALID;
  uint32_t state_ = ZX_EXCEPTION_STATE_TRY_NEXT;
  uint32_t strategy_ = ZX_EXCEPTION_STRATEGY_FIRST_CHANCE;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_EXCEPTION_H_
