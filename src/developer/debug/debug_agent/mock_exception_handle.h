
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_EXCEPTION_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_EXCEPTION_HANDLE_H_

#include <zircon/syscalls/exception.h>

#include <utility>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/developer/debug/debug_agent/exception_handle.h"
#include "src/developer/debug/debug_agent/mock_thread_handle.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

// ExceptionHandle abstracts zx::exception, allowing for a more straightforward
// implementation in tests in overrides of this class.
class MockExceptionHandle : public ExceptionHandle {
 public:
  MockExceptionHandle() = default;
  explicit MockExceptionHandle(uint64_t thread_koid,
                               debug_ipc::ExceptionType type = debug_ipc::ExceptionType::kGeneral)
      : thread_koid_(thread_koid), type_(type) {}
  ~MockExceptionHandle() = default;

  std::unique_ptr<ThreadHandle> GetThreadHandle() const override {
    return std::make_unique<MockThreadHandle>(thread_koid_);
  }

  debug_ipc::ExceptionType GetType(const ThreadHandle& thread) const override { return type_; }

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
  debug_ipc::ExceptionType type_ = debug_ipc::ExceptionType::kGeneral;
  uint32_t state_ = ZX_EXCEPTION_STATE_TRY_NEXT;
  uint32_t strategy_ = ZX_EXCEPTION_STRATEGY_FIRST_CHANCE;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_EXCEPTION_HANDLE_H_
