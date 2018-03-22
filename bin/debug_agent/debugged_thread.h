// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/public/lib/fxl/macros.h"

#include <zx/thread.h>

class DebugAgent;
class DebuggedProcess;

class DebuggedThread {
 public:
  // The SuspendReason indicates why the thread was suspended from our
  // perspective. This doesn't take into account other things on the system
  // that may have suspended a thread. If somebody does this, the thread will
  // be suspended but our state will be kNone (meaning resuming it is not
  // something we can do).
  enum class SuspendReason {
    // Not suspended.
    kNone,

    // Exception from the program.
    kException,

    // Hit a breakpoint.
    kBreakpoint,

    // Anything else.
    kOther
  };

  // When a thread is first created and we get a notification about it, it
  // will be suspended, but when we attach to a process with existing threads
  // it won't in in this state. The |starting| flag indicates that this is
  // a thread discoverd via a debug notification.
  DebuggedThread(DebuggedProcess* process, zx::thread thread,
                 zx_koid_t thread_koid, bool starting);
  ~DebuggedThread();

  zx::thread& thread() { return thread_; }
  zx_koid_t koid() const { return koid_; }
  SuspendReason suspend_reason() const { return suspend_reason_; }

  // Single steps this thread one instruction. The thread should currently
  // be in a stopped state. If it's not stopped, the step will be ignored.
  //void StepInstruction();

  void OnException(uint32_t type);

  // Continues execution of the thread. The thead should currently be in a
  // stopped state. If it's not stopped, this will be ignored.
  void Continue();

 private:
  DebugAgent* debug_agent_;  // Non-owning.
  DebuggedProcess* process_;  // Non-owning.
  zx::thread thread_;
  zx_koid_t koid_;

  // This is the reason for the thread suspend. This controls how the thread
  // will be resumed. Note that when a breakpoint is hit and other threads
  // are suspended, only the thread that hit the breakpoint will have
  // suspend_reason_ == kBreakpoint. The other threads will be manually
  // suspended which will be kOther.
  SuspendReason suspend_reason_ = SuspendReason::kNone;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedThread);
};
