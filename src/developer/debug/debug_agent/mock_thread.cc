// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_thread.h"

#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

MockThread::MockThread(DebuggedProcess* process, zx_koid_t thread_koid)
    : DebuggedThread(process, zx::thread(), thread_koid, zx::exception(),
                     ThreadCreationOption::kRunningKeepRunning) {}

void MockThread::ResumeException() {
  DEBUG_LOG(Test) << "ResumeException on " << koid();
  in_exception_ = false;
}

void MockThread::ResumeSuspension() {
  DEBUG_LOG(Test) << "ResumeSuspension on " << koid();
  suspended_ = false;
}

bool MockThread::Suspend(bool synchronous) {
  DEBUG_LOG(Test) << "Suspend on " << koid();
  suspended_ = true;
  return true;
}

bool MockThread::WaitForSuspension(zx::time deadline) {
  DEBUG_LOG(Test) << "WaitForSuspension on " << koid();
  return true;
}

}  // namespace debug_agent
