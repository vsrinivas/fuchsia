// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/address_ranges.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

StepOverThreadController::StepOverThreadController(StepMode mode,
                                                   FunctionReturnCallback function_return,
                                                   fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      step_mode_(mode),
      step_into_(std::make_unique<StepThreadController>(mode)),
      function_return_callback_(std::move(function_return)) {
  FX_DCHECK(mode != StepMode::kAddressRange);
}

StepOverThreadController::StepOverThreadController(AddressRanges ranges,
                                                   FunctionReturnCallback function_return,
                                                   fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      step_mode_(StepMode::kAddressRange),
      address_ranges_(ranges),
      step_into_(std::make_unique<StepThreadController>(std::move(ranges))),
      function_return_callback_(std::move(function_return)) {}

StepOverThreadController::~StepOverThreadController() = default;

void StepOverThreadController::InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) {
  SetThread(thread);

  Stack& stack = thread->GetStack();
  if (stack.empty()) {
    cb(Err("Can't step, no frames."));
    return;
  }

  // Save the info for the frame we're stepping inside of for future possible stepping out.
  frame_fingerprint_ = stack.GetFrameFingerprint(0);
  return_info_.InitFromTopOfStack(thread);
  if (step_mode_ == StepMode::kSourceLine) {
    // Always take the file/line from the frame rather than from LineDetails. In the case of
    // ambiguous inline locations, the LineDetails will contain only the innermost inline frame's
    // file/line, while the user could be stepping at a higher level where the frame's file line was
    // computed synthetically from the inline call hierarchy.
    file_line_ = stack[0]->GetLocation().file_line();
    Log("Stepping over %s:%d", file_line_.file().c_str(), file_line_.line());
  }

  // Stepping in the function itself is managed by the StepInto controller.
  step_into_->InitWithThread(thread, std::move(cb));
}

ThreadController::ContinueOp StepOverThreadController::GetContinueOp() {
  if (finish_)
    return finish_->GetContinueOp();
  return step_into_->GetContinueOp();
}

ThreadController::StopOp StepOverThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (finish_) {
    // Currently trying to step out of a sub-frame.
    if (auto op = finish_->OnThreadStop(stop_type, hit_breakpoints); op != kStopDone) {
      // Not done stepping out, keep working on it.
      Log("Still not done stepping out of sub-frame.");
      return op;
    }

    // Done stepping out. The "finish" operation is complete, but we may need to resume
    // single-stepping in the outer frame.
    Log("Done stepping out of sub-frame.");
    finish_.reset();
  } else {
    if (auto op = step_into_->OnThreadStop(stop_type, hit_breakpoints); op != kStopDone) {
      Log("Still in range after stepping.");
      return op;
    }
  }

  // If we just stepped into and out of a function, we could end up on the same line or in the same
  // address range as we started on and the user expects "step over" to keep going in that case.
  Stack& stack = thread()->GetStack();
  FrameFingerprint current_fingerprint = stack.GetFrameFingerprint(0);
  if (step_mode_ != StepMode::kInstruction && current_fingerprint == frame_fingerprint_) {
    // Same stack frame, do "step into" for the line again. This doesn't check the current line
    // itself since there is some special handling for things like "line 0" which we keep
    // encapsulated in the StepThreadController.
    Log("Doing a new StepController to keep going.");
    if (step_mode_ == StepMode::kSourceLine) {
      step_into_ = std::make_unique<StepThreadController>(file_line_);
    } else if (step_mode_ == StepMode::kAddressRange) {
      step_into_ = std::make_unique<StepThreadController>(address_ranges_);
    } else {
      FX_NOTREACHED();
    }

    step_into_->InitWithThread(thread(), [](const Err&) {});

    // Pass no exception type or breakpoints because we just want the step controller to evaluate
    // the current position regardless of how we got here.
    if (auto op = step_into_->OnThreadStop(debug_ipc::ExceptionType::kNone, {}); op != kStopDone)
      return op;

    // The step controller may have tweaked the stack, recompute the current fingerprint.
    current_fingerprint = stack.GetFrameFingerprint(0);
  }

  // The thread is no longer in range but could be in a different frame. It could be a newer frame
  // we need to step out of, or the same or older frame in which case we're done.
  if (frame_fingerprint_ == current_fingerprint) {
    // Same frame. Since we're not in range, this means we're done.
    Log("Step over complete, ended up in the same function.");
    return kStopDone;
  }
  if (FrameFingerprint::Newer(frame_fingerprint_, current_fingerprint)) {
    // Just stepped out of a function to an older frame, this means we're done and additionally
    // need to issue the return callback to indicate the function return.
    Log("Stepped out of the function, done.");
    if (function_return_callback_)
      function_return_callback_(return_info_);
    return kStopDone;
  }

  // This else case is that the current frame is newer than the frame we were stepping in. This
  // means we have to step out of the new frame to continue.

  if (stack.size() < 2) {
    Log("In a newer frame but there are not enough frames to step out.");
    return kStopDone;
  }

  // Got into a sub-frame. The calling code may have added a filter to stop at one of them.
  if (subframe_should_stop_callback_) {
    if (subframe_should_stop_callback_(stack[0])) {
      // Don't set the ambiguous inline frame in this case because we're in a subframe of the one we
      // were originally stepping in.
      Log("should_stop callback returned true, stopping.");
      return kStopDone;
    } else {
      Log("should_stop callback returned false, continuing.");
    }
  }

  // Begin stepping out of the sub-frame. The "finish" command initialization is technically
  // asynchronous since it's waiting for the breakpoint to be successfully set. Since we're
  // supplying an address to run to instead of a symbol, there isn't much that can go wrong other
  // than the process could be terminated out from under us or the memory is unmapped.
  //
  // These cases are catastrophic anyway so don't worry about those errors. Waiting for a full
  // round-trip to the debugged system for every function call in a "next" command would slow
  // everything down and make things more complex. It also means that the thread may be stopped if
  // the user asks for the state in the middle of a "next" command which would be surprising.
  //
  // Since the IPC will serialize the command, we know that successful breakpoint sets will arrive
  // before telling the thread to continue.
  Log("In a new frame, passing through to 'finish'.");
  finish_ = std::make_unique<FinishThreadController>(stack, 0);
  finish_->InitWithThread(thread(), [](const Err&) {});

  // Pass the "none" exception type here to bypass checking the exception type. The current
  // exception type may have been for the original "finish" controller above.
  return finish_->OnThreadStop(debug_ipc::ExceptionType::kNone, {});
}

}  // namespace zxdb
