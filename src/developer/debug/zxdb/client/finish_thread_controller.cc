// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/finish_physical_frame_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

FinishThreadController::FinishThreadController(Stack& stack, size_t frame_to_finish,
                                               FunctionReturnCallback cb,
                                               fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      frame_to_finish_(frame_to_finish),
      function_return_callback_(std::move(cb)),
      weak_factory_(this) {
  FX_DCHECK(frame_to_finish < stack.size());

  if (!stack[frame_to_finish]->IsInline()) {
    // Finishing a physical frame, don't need to do anything except forward to the physical version.
    finish_physical_controller_ = std::make_unique<FinishPhysicalFrameThreadController>(
        stack, frame_to_finish, [this](const FunctionReturnInfo& info) {
          // Forward the notification. Theoretically we could move the callback here but it seems
          // cleaner to consistently forward the notification because some thread controllers will
          // create more than one sub-controller needing a callback.
          if (function_return_callback_)
            function_return_callback_(info);
        });
    return;
  }

#ifndef NDEBUG
  // Stash for validation later.
  frame_ip_ = stack[frame_to_finish]->GetAddress();
#endif
}

FinishThreadController::~FinishThreadController() = default;

void FinishThreadController::InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) {
  SetThread(thread);

  if (finish_physical_controller_) {
    // Simple case where only a physical frame is being finished. The physical frame controller can
    // do everything.
    finish_physical_controller_->InitWithThread(thread, std::move(cb));
    return;
  }

  Stack& stack = thread->GetStack();

#ifndef NDEBUG
  // The stack must not have changed from construction to this call.
  FX_DCHECK(stack.size() > frame_to_finish_);
  FX_DCHECK(stack[frame_to_finish_]->GetAddress() == frame_ip_);
#endif

  if (enable_debug_logging()) {
    auto function = stack[frame_to_finish_]->GetLocation().symbol().Get()->As<Function>();
    if (function)
      Log("Finishing inline %s", function->GetFullName().c_str());
  }

  from_inline_frame_fingerprint_ = stack.GetFrameFingerprint(frame_to_finish_);

  // Find the next physical frame above the one being stepped out of.
  std::optional<size_t> found_physical_index;
  for (int i = static_cast<int>(frame_to_finish_) - 1; i >= 0; i--) {
    if (!stack[i]->IsInline()) {
      found_physical_index = i;
      break;
    }
  }
  if (found_physical_index) {
    // There is a physical frame above the one being stepped out of. Set up the physical frame
    // stepper to get out of it.
    finish_physical_controller_ = std::make_unique<FinishPhysicalFrameThreadController>(
        stack, *found_physical_index, [this](const FunctionReturnInfo& info) {
          // Forward the notification.
          if (function_return_callback_)
            function_return_callback_(info);
        });
    finish_physical_controller_->InitWithThread(thread, std::move(cb));
    return;
  }

  // There is no physical frame above the one being stepped out of, go to inline stepping to get out
  // of it.
  CreateInlineStepOverController(std::move(cb));
}

ThreadController::ContinueOp FinishThreadController::GetContinueOp() {
  if (step_over_line_0_controller_)
    return step_over_line_0_controller_->GetContinueOp();
  if (finish_physical_controller_)
    return finish_physical_controller_->GetContinueOp();
  return step_over_inline_controller_->GetContinueOp();
}

FinishThreadController::StopOp FinishThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  // "Line 0" stepping is the last phase. If that's set, that's all we need to do.
  if (step_over_line_0_controller_)
    return step_over_line_0_controller_->OnThreadStop(stop_type, hit_breakpoints);

  if (StopOp op = OnThreadStopFrameStepping(stop_type, hit_breakpoints); op != kStopDone)
    return op;

  // Done stepping out of all frames. Now we need to check whether we landed at some "line 0"
  // code (compiler generated without an associated line number) and step over that to get to the
  // next source code following the call.
  uint64_t ip = thread()->GetStack()[0]->GetAddress();
  LineDetails line_details = thread()->GetProcess()->GetSymbols()->LineDetailsForAddress(ip);
  if (!line_details.is_valid())
    return kStopDone;  // No line information here, stop.

  if (line_details.file_line().line() != 0)
    return kStopDone;  // Landed at some normal code, stop.

  // Step over the "line 0" code.
  step_over_line_0_controller_ = std::make_unique<StepThreadController>(StepMode::kSourceLine);
  step_over_line_0_controller_->InitWithThread(thread(), [](const Err&) {});
  // Don't forward exception type or breakpoints to this since we already consumed them above.
  return step_over_line_0_controller_->OnThreadStop(debug_ipc::ExceptionType::kNone, {});
}

FinishThreadController::StopOp FinishThreadController::OnThreadStopFrameStepping(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& input_hit_breakpoints) {
  // May need to get cleared before passing to sub-controllers.
  auto hit_breakpoints = input_hit_breakpoints;

  if (finish_physical_controller_) {
    Log("Dispatching to physical frame finisher.");
    if (auto op = finish_physical_controller_->OnThreadStop(stop_type, hit_breakpoints);
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

    // Clear the exception type and breakpoint information since it will typically be a software
    // breakpoint from the finish controller which the step controllers don't expect.
    stop_type = debug_ipc::ExceptionType::kNone;
    hit_breakpoints.clear();
  }

  if (step_over_inline_controller_) {
    // Have an existing step controller for an inline frame.
    Log("Dispatching to inline frame step over.");
    if (auto op = step_over_inline_controller_->OnThreadStop(stop_type, hit_breakpoints);
        op != kStopDone)
      return op;

    // Current step controller said stop so it's done.
    step_over_inline_controller_.reset();

    // As above, the exception and breakpoints have been "consumed" by the step over controller,
    // don't forward to the new one we're creating below.
    stop_type = debug_ipc::ExceptionType::kNone;
    hit_breakpoints.clear();
  }

  // See if there's an inline frame that needs stepping out of.
  Stack& stack = thread()->GetStack();
  FrameFingerprint current_fingerprint = stack.GetFrameFingerprint(0);
  if (!FrameFingerprint::NewerOrEqual(current_fingerprint, from_inline_frame_fingerprint_)) {
    Log("Not in a newer frame than the target, stopping.");
    return kStopDone;
  }

  // The top frame is newer than the desired destination so we need to step out of it. If the stack
  // hasn't changed in a surprising way all frames above the desired destination will be inline ones
  // that we can step out of with the "step over" controller.
  Log("Newer stack frame needs stepping out of.");
  if (!CreateInlineStepOverController([](const Err&) {}))
    return kStopDone;  // Something unexpected happened.
  return step_over_inline_controller_->OnThreadStop(stop_type, hit_breakpoints);
}

bool FinishThreadController::CreateInlineStepOverController(fit::callback<void(const Err&)> cb) {
  Stack& stack = thread()->GetStack();
  if (!stack[0]->IsInline()) {
    // The stack changed in an unexpected way and a newer physical frame appeared that we weren't
    // expecting. For now, report stop since something weird is going on. If this happens in
    // practice, the best thing to do is restart the step-out process with the physical frame step
    // out, followed by any inline ones.
    const char kMsg[] = "Unexpected non-inline frame when stepping out, giving up.";
    Log(kMsg);
    cb(Err(kMsg));
    return false;
  }

  const Location& location = stack[0]->GetLocation();
  const Function* func = location.symbol().Get()->As<Function>();
  if (!func) {
    const char kMsg[] = "No function symbol for inline frame, giving up.";
    Log(kMsg);
    cb(Err(kMsg));
    return false;
  }

  // Make a step over controller with the range of the inline function at the top of the stack.
  Log("Creating a new step over controller to get out of inline frame %s.",
      FrameFunctionNameForLog(stack[0]).c_str());
  step_over_inline_controller_ = std::make_unique<StepOverThreadController>(
      func->GetAbsoluteCodeRanges(location.symbol_context()));
  step_over_inline_controller_->InitWithThread(thread(), std::move(cb));
  return true;
}

}  // namespace zxdb
