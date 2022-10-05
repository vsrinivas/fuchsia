// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_EXCEPTION_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_EXCEPTION_HANDLE_H_

#include <lib/fit/result.h>

#include <memory>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/status.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

class ThreadHandle;

// ExceptionHandle abstracts zx::exception, allowing for a more straightforward implementation in
// tests in overrides of this class.
class ExceptionHandle {
 public:
  // How this exception should be resolved when closed.
  enum class Resolution { kTryNext, kHandled };

  ExceptionHandle() = default;
  virtual ~ExceptionHandle() = default;

  // Returns a handle to the excepting thread. Will return a null pointer on failure.
  virtual std::unique_ptr<ThreadHandle> GetThreadHandle() const = 0;

  // Returns the type of the exception for this and the current thread state.
  //
  // This requires getting the debug registers for the thread so the thread handle is passed in.
  // This could be implemented without the parameter because this object can create thread handles,
  // but that would be less efficient and all callers currently have existing ThreadHandles.
  virtual debug_ipc::ExceptionType GetType(const ThreadHandle& thread) const = 0;

  // Returns the current resolution for the exception.
  virtual fit::result<debug::Status, Resolution> GetResolution() const = 0;

  virtual debug::Status SetResolution(Resolution resolution) = 0;

  // Returns the associated the exception handling strategy.
  virtual fit::result<debug::Status, debug_ipc::ExceptionStrategy> GetStrategy() const = 0;

  // Sets the handling strategy.
  virtual debug::Status SetStrategy(debug_ipc::ExceptionStrategy strategy) = 0;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_EXCEPTION_HANDLE_H_
