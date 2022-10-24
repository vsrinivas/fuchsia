// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/function_thread_controller.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_through_plt_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

FunctionThreadController::FunctionThreadController(FunctionStep mode,
                                                   fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)), mode_(mode) {
  FX_DCHECK(mode != FunctionStep::kDefault);
}

void FunctionThreadController::InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) {
  SetThread(thread);

  switch (mode_) {
    case FunctionStep::kDefault:
      // Should not be hit.
      FX_DCHECK(false);
      cb(Err());
      break;
    case FunctionStep::kStepThroughPlt:
      sub_ = std::make_unique<StepThroughPltThreadController>();
      sub_->InitWithThread(thread, std::move(cb));
      break;
    case FunctionStep::kStepNoLineInfo:
      // No initialization necessary.
      cb(Err());
      break;
    case FunctionStep::kStepOut:
      // Delegate to the finish controller to get out of this function.
      sub_ = std::make_unique<FinishThreadController>(thread->GetStack(), 0);
      sub_->InitWithThread(thread, std::move(cb));
      break;
  }
}

ThreadController::ContinueOp FunctionThreadController::GetContinueOp() {
  if (sub_)
    return sub_->GetContinueOp();

  // Single-step as long as we are in unsymbolized code (everything else should be handled by the
  // "sub_" controller). Here, we can assume that the thread controller is not done, so the answer
  // is always to single-step instructions. The OnThreadStop() function will re-evaluate the
  // condition for the next one.
  FX_DCHECK(mode_ == FunctionStep::kStepNoLineInfo);
  return ContinueOp::StepInstruction();
}

ThreadController::StopOp FunctionThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (sub_)
    return sub_->OnThreadStop(stop_type, hit_breakpoints);

  // Single-step as long as we are in unsymbolized code (everything else should be handled by the
  // "sub_" controller).
  FX_DCHECK(mode_ == FunctionStep::kStepNoLineInfo);

  if (stop_type != debug_ipc::ExceptionType::kSingleStep)
    return kUnexpected;  // Something else happened, stop stepping.

  Stack& stack = thread()->GetStack();
  if (stack.empty()) {
    Log("StepThreadController unexpected");
    return kUnexpected;  // Bad state, give up trying to step.
  }

  // Get the line information. The stack will try to fix up "line 0" locations to use the next real
  // file/line in order to avoid showing "no line information" errors in the stack trace. This means
  // we can't trust the stack frame's location for making stepping decisions and should always use
  // the line details directly from the symbols.
  ProcessSymbols* process_symbols = thread()->GetProcess()->GetSymbols();
  LineDetails line_details = process_symbols->LineDetailsForAddress(stack[0]->GetAddress());

  // Single-step as long as there's unsymbolized lines.
  if (line_details.is_valid()) {
    Log("No longer on unsymbolized code, stopping.");
    return kStopDone;
  }
  Log("Still on unsymbolized code, stepping.");
  return kContinue;
}

}  // namespace zxdb
