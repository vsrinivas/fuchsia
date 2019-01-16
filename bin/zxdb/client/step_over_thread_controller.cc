// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_over_thread_controller.h"

#include "garnet/bin/zxdb/client/finish_thread_controller.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/step_thread_controller.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/symbols/line_details.h"
#include "garnet/bin/zxdb/symbols/process_symbols.h"
#include "lib/fxl/logging.h"

namespace zxdb {

StepOverThreadController::StepOverThreadController(StepMode mode)
    : step_into_(std::make_unique<StepThreadController>(mode)) {
  FXL_DCHECK(mode != StepMode::kAddressRange);
}

StepOverThreadController::StepOverThreadController(AddressRange range)
    : step_into_(std::make_unique<StepThreadController>(range)) {}

StepOverThreadController::~StepOverThreadController() = default;

void StepOverThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  if (thread->GetStack().empty()) {
    cb(Err("Can't step, no frames."));
    return;
  }

  // Save the info for the frame we're stepping inside of for future possible
  // stepping out.
  frame_fingerprint_ = thread->GetStack().GetFrameFingerprint(0);

  // Stepping in the function itself is managed by the StepInto controller.

  step_into_->InitWithThread(thread, std::move(cb));
}

ThreadController::ContinueOp StepOverThreadController::GetContinueOp() {
  if (finish_)
    return finish_->GetContinueOp();
  return step_into_->GetContinueOp();
}

ThreadController::StopOp StepOverThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (finish_) {
    // Currently trying to step out of a sub-frame.
    if (finish_->OnThreadStop(stop_type, hit_breakpoints) == kContinue) {
      // Not done stepping out, keep working on it.
      Log("Still not done stepping out of sub-frame.");
      return kContinue;
    }

    // Done stepping out. The "finish" operation is complete, but we may need
    // to resume single-stepping in the outer frame.
    Log("Done stepping out of sub-frame.");
    finish_.reset();

    // Ignore the stop type when giving control back to the "step into"
    // controller. In this case the stop type will be a software debug
    // exception (from the breakpoint inserted by the "finish" controller).
    // We want the "step into" controller to check for continuation even though
    // this stop type doesn't match what it's looking for.
    if (step_into_->OnThreadStopIgnoreType(hit_breakpoints) == kContinue) {
      Log("Still in range after stepping out.");
      return kContinue;
    }
  } else {
    if (step_into_->OnThreadStop(stop_type, hit_breakpoints) == kContinue) {
      Log("Still in range.");
      return kContinue;
    }
  }

  // If we get here the thread is no longer in range but could be in a sub-
  // frame that we need to step out of.
  FrameFingerprint current_fingerprint =
      thread()->GetStack().GetFrameFingerprint(0);
  if (!FrameFingerprint::Newer(current_fingerprint, frame_fingerprint_)) {
    Log("Neither in range nor in a newer frame.");
    return kStop;
  }

  const auto& stack = thread()->GetStack();
  if (stack.size() < 2) {
    Log("In a newer frame but there are not enough frames to step out.");
    return kStop;
  }

  // Got into a sub-frame. The calling code may have added a filter to stop
  // at one of them.
  if (subframe_should_stop_callback_) {
    if (subframe_should_stop_callback_(stack[0])) {
      Log("should_stop callback returned true, stopping.");
      return kStop;
    } else {
      Log("should_stop callback returned false, continuing.");
    }
  }

  // Begin stepping out of the sub-frame. The "finish" command initialization
  // is technically asynchronous since it's waiting for the breakpoint to be
  // successfully set. Since we're supplying an address to run to instead of a
  // symbol, there isn't much that can go wrong other than the process could
  // be terminated out from under us or the memory is unmapped.
  //
  // These cases are catastrophic anyway so don't worry about those errors.
  // Waiting for a full round-trip to the debugged system for every function
  // call in a "next" command would slow everything down and make things
  // more complex. It also means that the thread may be stopped if the user
  // asks for the state in the middle of a "next" command which would be
  // surprising.
  //
  // Since the IPC will serialize the command, we know that successful
  // breakpoint sets will arrive before telling the thread to continue.
  Log("In a new frame, passing through to 'finish'.");
  finish_ = std::make_unique<FinishThreadController>(
      FinishThreadController::ToFrame(), stack[1]->GetAddress(),
      frame_fingerprint_);
  finish_->InitWithThread(thread(), [](const Err&) {});
  return finish_->OnThreadStop(stop_type, hit_breakpoints);
}

}  // namespace zxdb
