// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_thread.h"

#include "src/developer/debug/debug_agent/mock_exception_handle.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

MockThread::MockThread(DebuggedProcess* process, zx_koid_t thread_koid)
    : DebuggedThread(process->debug_agent(), process,
                     std::make_unique<MockThreadHandle>(thread_koid)) {}

MockThread::~MockThread() {}

void MockThread::SendException(uint64_t address, debug_ipc::ExceptionType type) {
  GeneralRegisters regs;
  regs.set_ip(address);
  mock_thread_handle().SetGeneralRegisters(regs);

  mock_thread_handle().set_state(
      ThreadHandle::State(debug_ipc::ThreadRecord::BlockedReason::kException));

  OnException(std::make_unique<MockExceptionHandle>(koid(), type));
}

}  // namespace debug_agent
