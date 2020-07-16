// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_thread.h"

#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_thread_exception.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

MockThread::MockThread(DebuggedProcess* process, zx_koid_t thread_koid)
    : DebuggedThread(nullptr,
                     DebuggedThread::CreateInfo{process, thread_koid,
                                                std::make_unique<MockThreadHandle>(thread_koid),
                                                ThreadCreationOption::kRunningKeepRunning,
                                                std::make_unique<MockThreadException>()}) {}

MockThread::~MockThread() {}

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

void MockThread::IncreaseSuspend() {
  suspend_count_++;
  DEBUG_LOG(Test) << "Thread " << koid() << ": Increased suspend count to " << suspend_count_;
}

void MockThread::DecreaseSuspend() {
  FX_DCHECK(suspend_count_ > 0);
  suspend_count_--;
  DEBUG_LOG(Test) << "Thread " << koid() << ": Decreased suspend count to " << suspend_count_;
}
}  // namespace debug_agent
