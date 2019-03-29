// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Delegate interface is how we give clients control over what happens
// when something happens to the inferior.
// The default behaviour, provided by the baseclass, is to run the inferior
// and if it gets an exception then print a backtrace and kill it.

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/lib/debugger_utils/breakpoints.h"

#include "delegate.h"
#include "process.h"
#include "server.h"
#include "thread.h"

namespace inferior_control {

void Delegate::OnThreadStarting(Process* process, Thread* thread,
                                zx_handle_t eport,
                                const zx_exception_context_t& context) {
  if (!thread->ResumeFromException(eport)) {
    // If resumption fails something is really wrong. Do a graceful exit.
    // E.g., someone could have terminated the process in the interim.
    FXL_LOG(ERROR) << "Unable to resume thread " << thread->GetName()
                   << " after start notification";
    FXL_LOG(ERROR) << "Process will be killed, no point in continuing";
    process->Kill();
  }
}

void Delegate::OnThreadExiting(Process* process, Thread* thread,
                               zx_handle_t eport,
                               const zx_exception_context_t& context) {
  thread->ResumeForExit(eport);
}

void Delegate::OnThreadSuspension(Thread* thread) {
  // Nothing to do by default.
  // This doesn't resume the thread, we don't have the suspend token.
}

void Delegate::OnThreadResumption(Thread* thread) {
  // Nothing to do by default.
}

void Delegate::OnThreadTermination(Thread* thread) {
  // Nothing to do by default.
}

void Delegate::OnProcessTermination(Process* process) {
  // "true" here indicates we completed successfully. Whether the inferior
  // completed successfully is a separate question and can be determined by
  // looking at its return code.
  server_->PostQuitMessageLoop(true);
}

void Delegate::OnArchitecturalException(
    Process* process, Thread* thread, zx_handle_t eport,
    zx_excp_type_t type, const zx_exception_context_t& context) {
  // There is one exception we need to handle: the ld.so breakpoint.
  // The DSO list hasn't been loaded yet, it's our responsibility to do so.
  // This is one place where we deviate from the goal of having internal
  // state updated before Delegate methods are called. The reason is that
  // generally the client will want to resume after this s/w breakpoint, but
  // only this one, not any further ones.
  //
  // DSO loading is currently only managed at startup.
  // This is done by setting ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET which causes
  // a s/w breakpoint instruction to be executed after all DSOs are loaded.
  // TODO(dje): Handle case of hitting a breakpoint before then (highly
  // unlikely, but technically possible).
  // TODO(dje): dlopen.
  if (type == ZX_EXCP_SW_BREAKPOINT) {
    if (process->CheckDsosList(thread)) {
      // At ld.so breakpoint, DSO list loaded.
      zx_status_t status =
        debugger_utils::ResumeAfterSoftwareBreakpointInstruction(
            thread->handle(), eport);
      if (status != ZX_OK) {
        // This is a breakpoint we introduced. No point in passing it on to
        // other handlers. If resumption fails there's not much we can do.
        // E.g., Someone could have terminated the process in the interim.
        FXL_LOG(ERROR) << "Unable to resume thread " << thread->GetName()
                       << " after ld.so breakpoint, status: "
                       << debugger_utils::ZxErrorString(status);
        FXL_LOG(ERROR) << "Process will be killed, no point in continuing";
        process->Kill();
      }
      return;
    }
  }

  thread->Dump();
  process->Kill();
}

void Delegate::OnSyntheticException(
    Process* process, Thread* thread, zx_handle_t eport,
    zx_excp_type_t type, const zx_exception_context_t& context) {
  thread->Dump();
  process->Kill();
}

}  // namespace inferior_control
