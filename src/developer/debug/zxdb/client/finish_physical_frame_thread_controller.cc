// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/finish_physical_frame_thread_controller.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/until_thread_controller.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/function.h"

namespace zxdb {

FinishPhysicalFrameThreadController::FinishPhysicalFrameThreadController(
    Stack& stack, size_t frame_to_finish, FunctionReturnCallback cb, fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      frame_to_finish_(frame_to_finish),
      function_return_callback_(std::move(cb)),
      weak_factory_(this) {
  FX_DCHECK(frame_to_finish < stack.size());
  FX_DCHECK(!stack[frame_to_finish]->IsInline());

  // Save the symbol being finished for later notifications.
  function_being_finished_ = stack[frame_to_finish]->GetLocation().symbol();

#ifndef NDEBUG
  // Stash for validation later.
  frame_ip_ = stack[frame_to_finish]->GetAddress();
#endif
}

FinishPhysicalFrameThreadController::~FinishPhysicalFrameThreadController() = default;

FinishPhysicalFrameThreadController::StopOp FinishPhysicalFrameThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (until_controller_) {
    if (auto op = until_controller_->OnThreadStop(stop_type, hit_breakpoints); op != kStopDone)
      return op;

    // The until controller said to stop. The CPU is now at the address immediately following the
    // function call. The tricky part is that this could be the first instruction of a new inline
    // function following the call and the stack will now contain that inline expansion. Our caller
    // expects to be in the frame that called the function being stepped out of.
    //
    // Rolling ambiguous frames back to "one before" the frame fingerprint being finished might
    // sound right but isn't because that fingerprint won't exist any more (we just exited it).
    //
    // For a frame to be ambiguous the IP must be at the first instruction of a range of that
    // inline. By virtue of just returning from a function call, we know any inline functions that
    // start immediately after the call weren't in the stack of the original call.
    Stack& stack = thread()->GetStack();
    stack.SetHideAmbiguousInlineFrameCount(stack.GetAmbiguousInlineFrameCount());

    if (function_return_callback_) {
      function_return_callback_(
          FunctionReturnInfo{.thread = thread(), .symbol = function_being_finished_});
    }

    return kStopDone;
  }

  // When there's no "until" controller, this controller just said "continue" to step out of the
  // oldest stack frame. Therefore, any stops at this level aren't ours.
  return kContinue;
}

void FinishPhysicalFrameThreadController::InitWithThread(Thread* thread,
                                                         fit::callback<void(const Err&)> cb) {
  SetThread(thread);

  Stack& stack = thread->GetStack();

#ifndef NDEBUG
  // The stack must not have changed from construction to this call. There are no async requests
  // that need to happen during this time, just registration with the thread. Otherwise the frame
  // fingerprint computation needs to be scheduled in the constructor which complicates the async
  // states of this function (though it's possible in the future if necessary).
  FX_DCHECK(stack.size() > frame_to_finish_);
  FX_DCHECK(stack[frame_to_finish_]->GetAddress() == frame_ip_);
#endif

  if (enable_debug_logging()) {
    auto function = stack[frame_to_finish_]->GetLocation().symbol().Get()->As<Function>();
    if (function)
      Log("Finishing %s", function->GetFullName().c_str());
    else
      Log("Finshing unsymbolized function");
  }

  InitWithFingerprint(stack.GetFrameFingerprint(frame_to_finish_));
  cb(Err());
}

ThreadController::ContinueOp FinishPhysicalFrameThreadController::GetContinueOp() {
  // Once this thread starts running, the frame index is invalid.
  frame_to_finish_ = static_cast<size_t>(-1);

  if (until_controller_)
    return until_controller_->GetContinueOp();

  // This will happen when there's no previous frame so there's no address to return to.
  // Unconditionally continue.
  return ContinueOp::Continue();
}

void FinishPhysicalFrameThreadController::InitWithFingerprint(FrameFingerprint fingerprint) {
  if (frame_to_finish_ >= thread()->GetStack().size() - 1) {
    // Finishing the last frame. There is no return address so there's no setup necessary to step,
    // just continue.
    return;
  }

  // The address we're returning to is that of the previous frame,
  uint64_t to_addr = thread()->GetStack()[frame_to_finish_ + 1]->GetAddress();
  if (!to_addr)
    return;  // Previous stack frame is null, just continue.

  until_controller_ = std::make_unique<UntilThreadController>(
      std::vector<InputLocation>{InputLocation(to_addr)}, fingerprint,
      UntilThreadController::kRunUntilOlderFrame);

  // Give the "until" controller a dummy callback and execute the callback ASAP. The until
  // controller executes the callback once it knows that the breakpoint set has been complete
  // (round-trip to the target system).
  //
  // Since we provide an address there's no weirdness with symbols and we don't have to worry about
  // matching 0 locations. If the breakpoint set fails, the caller address is invalid and stepping
  // is impossible so it doesn't matter. We can run faster without waiting for the round-trip, and
  // the IPC will serialize so the breakpoint set happens before the thread resume.
  until_controller_->InitWithThread(thread(), [](const Err&) {});
}

}  // namespace zxdb
