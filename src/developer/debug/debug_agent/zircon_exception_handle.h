// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_EXCEPTION_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_EXCEPTION_HANDLE_H_

#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>

#include <utility>

#include "src/developer/debug/debug_agent/exception_handle.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

// Wraps a zx::exception, which is expected to be valid for the lifetime of an instance of this
// class.
class ZirconExceptionHandle : public ExceptionHandle {
 public:
  ZirconExceptionHandle(zx::exception exception, const zx_exception_info_t& info)
      : exception_(std::move(exception)), info_(info) {}

  ~ZirconExceptionHandle() = default;

  std::unique_ptr<ThreadHandle> GetThreadHandle() const override;
  debug_ipc::ExceptionType GetType(const ThreadHandle& thread) const override;
  fitx::result<zx_status_t, Resolution> GetResolution() const override;
  zx_status_t SetResolution(Resolution state) override;
  fitx::result<zx_status_t, debug_ipc::ExceptionStrategy> GetStrategy() const override;
  zx_status_t SetStrategy(debug_ipc::ExceptionStrategy strategy) override;

 private:
  zx::exception exception_;
  zx_exception_info_t info_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ZirconExceptionHandle);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_EXCEPTION_HANDLE_H_
