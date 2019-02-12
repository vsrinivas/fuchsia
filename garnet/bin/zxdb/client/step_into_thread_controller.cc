// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_into_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/step_thread_controller.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"
#include "lib/fxl/logging.h"

namespace zxdb {

StepIntoThreadController::StepIntoThreadController(StepMode mode)
    : mode_(mode) {}

StepIntoThreadController::~StepIntoThreadController() = default;

void StepIntoThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);
  cb(Err());
}

ThreadController::ContinueOp StepIntoThreadController::GetContinueOp() {
  // If the regular step controller already exists, we're already in the middle
  // of a real step so forward everything to it.
  if (step_controller_)
    return step_controller_->GetContinueOp();

  // Otherwise this must be the first call and we need to consider whether
  // to do a magic inline step.
  if (TrySteppingIntoInline())
    return ContinueOp::SyntheticStop();

  // Not a sythetic "step into", make a regular step controller and forward
  // to it.
  //
  // This could have been done in InitWithThread() which would be conceptually
  // nicer because the callback can be forwarded. But the step controller
  // doesn't need to do any important asynchronous initialization and creating
  // the sub-controller here consolidates all logic into this one function.
  Log("No inline frame to step into, resuming with physical step.");
  step_controller_ = std::make_unique<StepThreadController>(mode_);
  step_controller_->set_stop_on_no_symbols(stop_on_no_symbols_);
  step_controller_->InitWithThread(thread(), [](const Err&) {});
  return step_controller_->GetContinueOp();
}

ThreadController::StopOp StepIntoThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  // All real stops should result from using the real step controller.
  FXL_DCHECK(step_controller_);
  return step_controller_->OnThreadStop(stop_type, hit_breakpoints);
}

bool StepIntoThreadController::TrySteppingIntoInline() {
  if (mode_ != StepMode::kSourceLine) {
    // Only do inline frame handling when stepping by line.
    //
    // When the user is doing a single-instruction step, ignore special inline
    // frames and always do a real step. The other mode is "address range"
    // which isn't exposed to the user directly so we probably won't encounter
    // it here, but assume that it's also a low-level operation that doesn't
    // need inline handling.
    return false;
  }

  Stack& stack = thread()->GetStack();

  size_t hidden_frame_count = stack.hide_ambiguous_inline_frame_count();
  if (hidden_frame_count == 0) {
    // The Stack object always contains all inline functions nested at the
    // current address. When it's not logically in one or more of them, they
    // will be hidden. Not having any hidden inline frames means there's
    // nothing to a synthetic inline step into.
    return false;
  }

  // Examine the closest hidden frame.
  const Frame* frame =
      stack.FrameAtIndexIncludingHiddenInline(hidden_frame_count - 1);
  if (!frame->IsAmbiguousInlineLocation())
    return false;  // No inline or not ambiguous.

  // Do the synthetic step into by unhiding an inline frame.
  size_t new_hide_count = hidden_frame_count - 1;
  Log("Synthetically stepping into inline frame, new hide count = %zu.",
      new_hide_count);
  stack.SetHideAmbiguousInlineFrameCount(new_hide_count);
  return true;
}

}  // namespace zxdb
