// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_thread.h"

#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

MockThread::MockThread(DebuggedProcess* process, zx_koid_t thread_koid,
                       std::shared_ptr<ObjectProvider> object_provider,
                       std::shared_ptr<arch::ArchProvider> arch_provider)
    : DebuggedThread(process, zx::thread(), thread_koid, zx::exception(),
                     ThreadCreationOption::kRunningKeepRunning, std::move(object_provider),
                     std::move(arch_provider)) {}

void MockThread::ResumeException() {
  DEBUG_LOG(Test) << "Thread " << koid() << ": Resuming exception.";
  in_exception_ = false;
}

void MockThread::ResumeSuspension() {
  DEBUG_LOG(Test) << "Thread " << koid() << ": Resuming suspension.";
  internal_suspension_ = false;
}

bool MockThread::Suspend(bool synchronous) {
  DEBUG_LOG(Test) << "Thread " << koid() << ": Suspend on " << koid();
  internal_suspension_ = true;
  return true;
}

bool MockThread::WaitForSuspension(zx::time deadline) {
  DEBUG_LOG(Test) << "WaitForSuspension on " << koid();
  return true;
}

void MockThread::FillThreadRecord(debug_ipc::ThreadRecord::StackAmount stack_amount,
                                  const zx_thread_state_general_regs* optional_regs,
                                  debug_ipc::ThreadRecord* record) const {
  record->process_koid = process()->koid();
  record->thread_koid = koid();
}

void MockThread::IncreaseSuspend() {
  suspend_count_++;
  DEBUG_LOG(Test) << "Thread " << koid() << ": Increased suspend count to " << suspend_count_;
}

void MockThread::DecreaseSuspend() {
  FXL_DCHECK(suspend_count_ > 0);
  suspend_count_--;
  DEBUG_LOG(Test) << "Thread " << koid() << ": Decreased suspend count to " << suspend_count_;
}
}  // namespace debug_agent
