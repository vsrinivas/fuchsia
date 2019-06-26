// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/debug_agent/debugged_thread.h"

namespace debug_agent {

class MockThread : public DebuggedThread {
 public:
  MockThread(DebuggedProcess* process, zx_koid_t thread_koid);

  void ResumeException() override;
  void ResumeSuspension() override;

  bool Suspend(bool synchronous = false) override;
  bool WaitForSuspension(zx::time deadline = DefaultSuspendDeadline()) override;

  bool IsSuspended() const override { return suspended_; }
  bool IsInException() const override { return in_exception_; }

 private:
  bool suspended_ = false;
  bool in_exception_ = false;
};

}  // namespace debug_agent
