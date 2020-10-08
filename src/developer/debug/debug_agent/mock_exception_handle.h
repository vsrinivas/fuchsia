
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_EXCEPTION_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_EXCEPTION_HANDLE_H_

#include <zircon/status.h>

#include <functional>
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
  // std::function and not fit::, as it is more convenient for test logic to
  // have MockExceptionHandle as copyable.
  using SetStateCallback = std::function<void(Resolution)>;
  using SetStrategyCallback = std::function<void(debug_ipc::ExceptionStrategy)>;

  MockExceptionHandle() = default;

  explicit MockExceptionHandle(uint64_t thread_koid,
                               debug_ipc::ExceptionType type = debug_ipc::ExceptionType::kGeneral)
      : thread_koid_(thread_koid), type_(type) {}

  MockExceptionHandle(SetStateCallback on_state_change, SetStrategyCallback on_strategy_change)
      : on_state_change_(std::move(on_state_change)),
        on_strategy_change_(std::move(on_strategy_change)) {}

  ~MockExceptionHandle() = default;

  std::unique_ptr<ThreadHandle> GetThreadHandle() const override {
    return std::make_unique<MockThreadHandle>(thread_koid_);
  }

  debug_ipc::ExceptionType GetType(const ThreadHandle& thread) const override { return type_; }

  void set_type(debug_ipc::ExceptionType type) { type_ = type; }

  fitx::result<zx_status_t, Resolution> GetResolution() const override {
    return fitx::ok(resolution_);
  }

  zx_status_t SetResolution(Resolution resolution) override {
    resolution_ = resolution;
    on_state_change_(resolution);
    return ZX_OK;
  }

  fitx::result<zx_status_t, debug_ipc::ExceptionStrategy> GetStrategy() const override {
    return fitx::ok(strategy_);
  }

  zx_status_t SetStrategy(debug_ipc::ExceptionStrategy strategy) override {
    strategy_ = strategy;
    on_strategy_change_(strategy);
    return ZX_OK;
  }

 private:
  uint64_t thread_koid_ = ZX_KOID_INVALID;
  debug_ipc::ExceptionType type_ = debug_ipc::ExceptionType::kGeneral;
  Resolution resolution_ = Resolution::kTryNext;
  debug_ipc::ExceptionStrategy strategy_ = debug_ipc::ExceptionStrategy::kFirstChance;
  SetStateCallback on_state_change_ = [](Resolution) {};
  SetStrategyCallback on_strategy_change_ = [](debug_ipc::ExceptionStrategy) {};
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_EXCEPTION_HANDLE_H_
