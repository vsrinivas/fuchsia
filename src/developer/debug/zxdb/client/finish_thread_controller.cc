// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"

#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/client/finish_physical_frame_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

FinishThreadController::FinishThreadController(Stack& stack,
                                               size_t frame_to_finish)
    : frame_to_finish_(frame_to_finish), weak_factory_(this) {
  FXL_DCHECK(frame_to_finish < stack.size());

  if (!stack[frame_to_finish]->IsInline()) {
    // Finishing a physical frame, don't need to do anything except forward
    // to the physical version.
    finish_physical_controller_ =
        std::make_unique<FinishPhysicalFrameThreadController>(stack,
                                                              frame_to_finish);
    return;
  }

#ifndef NDEBUG
  // Stash for validation later.
  frame_ip_ = stack[frame_to_finish]->GetAddress();
#endif
}

FinishThreadController::~FinishThreadController() = default;

FinishThreadController::StopOp FinishThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (finish_physical_controller_) {
    Log("Dispatching to physical frame finisher.");
    if (auto op = finish_physical_controller_->OnThreadStop(stop_type,
                                                            hit_breakpoints);
        op != kStopDone)
      return op;  // Still stepping out of the physical frame.

    // Physical frame controller said stop.
    finish_physical_controller_.reset();

    // May need to step out of some inline frames now.
    if (!from_inline_frame_fingerprint_.is_valid()) {
      Log("No inline frames to step out of, 'finish' is done.");
      Log("  inline frames = %zu, hidden = %zu",
          thread()->GetStack().GetAmbiguousInlineFrameCount(),
          thread()->GetStack().hide_ambiguous_inline_frame_count());
      return kStopDone;  // No inline frames to step out of, we're done.
    }

    // Clear the exception type since it will typically be a software
    // breakpoint from the finish controller which the step controllers don't
    // expect.
    stop_type = debug_ipc::NotifyException::Type::kNone;
  }

  if (step_over_controller_) {
    // Have an existing step controller for an inline frame.
    Log("Dispatching to inline frame step over.");
    if (auto op =
            step_over_controller_->OnThreadStop(stop_type, hit_breakpoints);
        op != kStopDone)
      return op;

    // Current step controller said stop so it's done.
    step_over_controller_.reset();
  }

  // See if there's an inline frame that needs stepping out of.
  Stack& stack = thread()->GetStack();
  FrameFingerprint current_fingerprint = *stack.GetFrameFingerprint(0);
  if (!FrameFingerprint::NewerOrEqual(current_fingerprint,
                                      from_inline_frame_fingerprint_)) {
    Log("Not in a newer frame than the target, stopping.");
    return kStopDone;
  }

  // The top frame is newer than the desired destination so we need to
  // step out of it. If the stack hasn't changed in a surprising way all
  // frames above the desired destination will be inline ones that we can
  // step out of with the "step over" controller.
  Log("Newer stack frame needs stepping out of.");
  if (!CreateInlineStepOverController([](const Err&) {}))
    return kStopDone;  // Something unexpected happened.
  return step_over_controller_->OnThreadStop(stop_type, hit_breakpoints);
}

void FinishThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  if (finish_physical_controller_) {
    // Simple case where only a physical frame is being finished. The physical
    // frame controller can do everything.
    finish_physical_controller_->InitWithThread(thread, std::move(cb));
    return;
  }

  Stack& stack = thread->GetStack();

#ifndef NDEBUG
  // The stack must not have changed from construction to this call.
  FXL_DCHECK(stack.size() > frame_to_finish_);
  FXL_DCHECK(stack[frame_to_finish_]->GetAddress() == frame_ip_);
#endif

#ifdef DEBUG_THREAD_CONTROLLERS
  auto function =
      stack[frame_to_finish_]->GetLocation().symbol().Get()->AsFunction();
  if (function)
    Log("Finishing inline %s", function->GetFullName().c_str());
#endif

  auto found_fingerprint = stack.GetFrameFingerprint(frame_to_finish_);
  if (!found_fingerprint) {
    // This can happen if the creator of this class requested that we finish
    // the bottom-most stack frame available, without having all stack frames
    // available. That's not allowed and any code doing that should be fixed.
    FXL_NOTREACHED();
    cb(
        Err("Trying to step out of an inline frame with insufficient context.\n"
            "Please file a bug with a repro."));
    return;
  }
  from_inline_frame_fingerprint_ = *found_fingerprint;

  // Find the next physical frame above the one being stepped out of.
  std::optional<size_t> found_physical_index;
  for (int i = static_cast<int>(frame_to_finish_) - 1; i >= 0; i--) {
    if (!stack[i]->IsInline()) {
      found_physical_index = i;
      break;
    }
  }
  if (found_physical_index) {
    // There is a physical frame above the one being stepped out of. Set up
    // the physical frame stepper to get out of it.
    finish_physical_controller_ =
        std::make_unique<FinishPhysicalFrameThreadController>(
            stack, *found_physical_index);
    finish_physical_controller_->InitWithThread(thread, std::move(cb));
    return;
  }

  // There is no physical frame above the one being stepped out of, go to
  // inline stepping to get out of it.
  CreateInlineStepOverController(std::move(cb));
}

ThreadController::ContinueOp FinishThreadController::GetContinueOp() {
  if (finish_physical_controller_)
    return finish_physical_controller_->GetContinueOp();
  return step_over_controller_->GetContinueOp();
}

bool FinishThreadController::CreateInlineStepOverController(
    std::function<void(const Err&)> cb) {
  Stack& stack = thread()->GetStack();
  if (!stack[0]->IsInline()) {
    // The stack changed in an unexpected way and a newer physical frame
    // appeared that we weren't expecting. For now, report stop since
    // something weird is going on. If this happens in practice, the best
    // thing to do is restart the step-out process with the physical frame
    // step out, followed by any inline ones.
    const char kMsg[] =
        "Unexpected non-inline frame when stepping out, giving up.";
    Log(kMsg);
    cb(Err(kMsg));
    return false;
  }

  const Location& location = stack[0]->GetLocation();
  const Function* func = location.symbol().Get()->AsFunction();
  if (!func) {
    const char kMsg[] = "No function symbol for inline frame, giving up.";
    Log(kMsg);
    cb(Err(kMsg));
    return false;
  }

  // Make a step over controller with the range of the inline function at
  // the top of the stack.
  Log("Creating a new step over controller to get out of inline frame %s.",
      FrameFunctionNameForLog(stack[0]).c_str());
  step_over_controller_ = std::make_unique<StepOverThreadController>(
      func->GetAbsoluteCodeRanges(location.symbol_context()));
  step_over_controller_->InitWithThread(thread(), std::move(cb));
  return true;
}

}  // namespace zxdb
