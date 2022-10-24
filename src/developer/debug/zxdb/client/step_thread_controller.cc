// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_thread_controller.h"

#include <inttypes.h>

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/function_step.h"
#include "src/developer/debug/zxdb/client/function_thread_controller.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

StepThreadController::StepThreadController(StepMode mode, FunctionReturnCallback function_return,
                                           fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      step_mode_(mode),
      function_return_callback_(std::move(function_return)) {}

StepThreadController::StepThreadController(const FileLine& line,
                                           FunctionReturnCallback function_return,
                                           fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      step_mode_(StepMode::kSourceLine),
      file_line_(line),
      function_return_callback_(std::move(function_return)) {}

StepThreadController::StepThreadController(AddressRanges ranges,
                                           FunctionReturnCallback function_return,
                                           fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      step_mode_(StepMode::kAddressRange),
      current_ranges_(ranges),
      function_return_callback_(std::move(function_return)) {}

StepThreadController::~StepThreadController() = default;

void StepThreadController::InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) {
  SetThread(thread);

  const Stack& stack = thread->GetStack();
  if (stack.empty()) {
    cb(Err("Can't step, no frames."));
    return;
  }
  const Frame* top_frame = stack[0];
  uint64_t ip = top_frame->GetAddress();

  if (step_mode_ == StepMode::kSourceLine) {
    if (!file_line_) {
      // Always take the file/line from the stack rather than the line table. The stack will have
      // been fixed up and may reference the calling line for an inline routine, while the line
      // table will reference the inlined source that generated the instructions.
      file_line_ = top_frame->GetLocation().file_line();
    }

    LineDetails line_details = thread->GetProcess()->GetSymbols()->LineDetailsForAddress(ip);
    if (line_details.file_line() == *file_line_) {
      // When the stack and the line details match up, the range from the line table is usable.
      current_ranges_ = AddressRanges(line_details.GetExtent());
      Log("Stepping in %s:%d %s", file_line_->file().c_str(), file_line_->line(),
          current_ranges_.ToString().c_str());
    } else {
      // Otherwise keep the current range empty to cause a step into inline routine or potentially a
      // single step.
      current_ranges_ = AddressRanges();
      Log("Stepping in empty range.");
    }

  } else {
    // In the "else" cases, the range will already have been set up.
    Log("Stepping in %s", current_ranges_.ToString().c_str());
  }

  original_frame_fingerprint_ = thread->GetStack().GetFrameFingerprint(0);
  return_info_.InitFromTopOfStack(thread);

  cb(Err());
}

ThreadController::ContinueOp StepThreadController::GetContinueOp() {
  if (function_step_)
    return function_step_->GetContinueOp();

  // The stack shouldn't be empty when stepping in a range, give up if it is.
  const auto& stack = thread()->GetStack();
  if (stack.empty()) {
    Log("Declaring synthetic stop due to empty stack.");
    return ContinueOp::SyntheticStop();
  }

  // Check for inlines. This case will likely have an empty address range so the inline check needs
  // to be done before checking for empty ranges below.
  //
  // GetContinueOp() should not modify thread state, so we need to return whether we want to modify
  // the inline stack. Returning SyntheticStop here will schedule a call OnThreadStop with a
  // synthetic exception. The inline stack should actually be modified at that point.
  if (TrySteppingIntoInline(StepIntoInline::kQuery)) {
    Log("Declaring synthetic stop due to inline.");
    return ContinueOp::SyntheticStop();
  }

  // An empty range means to step by instruction.
  if (current_ranges_.empty())
    return ContinueOp::StepInstruction();

  // Use the IP from the top of the stack to figure out which range to send to the agent (it only
  // accepts one, while we can have a set).
  if (auto inside = current_ranges_.GetRangeContaining(stack[0]->GetAddress()))
    return ContinueOp::StepInRange(*inside);

  // Don't generally expect to be continuing in a range that we're not currently inside of. But it
  // could be the caller is expecting the next instruction to be in that range, so fall back to
  // single-step mode.
  return ContinueOp::StepInstruction();
}

ThreadController::StopOp StepThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  Log("StepThreadController::OnThreadStop");
  Stack& stack = thread()->GetStack();
  if (stack.empty()) {
    Log("StepThreadController unexpected");
    return kUnexpected;  // Agent sent bad state, give up trying to step.
  }

  if (function_step_) {
    if (function_step_->OnThreadStop(stop_type, hit_breakpoints) == kContinue)
      return kContinue;

    Log("Function sub-thread-controller reported done, resuming evaluation.");
    function_step_.reset();
  } else {
    // The only real exception type we care about (as opposed to synthetic and "none" -- see below)
    // are the single step exceptions. We wouldn't want to try to resume from a crash just because
    // it's in our range, or if there was a hardcoded debug instruction in the range, for example.
    //
    // This must happen only when there's no "finish" controller since a successful "finish" hit
    // will have a software breakpoint.
    //
    // A "none" type means to ignore the exception type and evaluate the current code location. It
    // is used when this controller is nested. A synthetic exception is used to step into inline
    // functions.
    if ((stop_type != debug_ipc::ExceptionType::kNone &&
         stop_type != debug_ipc::ExceptionType::kSynthetic &&
         stop_type != debug_ipc::ExceptionType::kSingleStep) ||
        !hit_breakpoints.empty()) {
      Log("Not our exception type, stop is somebody else's.");
      return kUnexpected;
    }
  }

  if (stop_type == debug_ipc::ExceptionType::kSynthetic ||
      stop_type == debug_ipc::ExceptionType::kNone) {
    // Handle virtually stepping into inline functions by modifying the hidden ambiguous inline
    // frame count.
    //
    // This should happen for synthetic stops because modifying the hide count is an alternative to
    // actually stepping the CPU. Doing this after a real step will modify the stack for the *next*
    // instruction (like doing "step into" twice in the case of ambiguous inline frames).
    if (TrySteppingIntoInline(StepIntoInline::kCommit))
      return kStopDone;

    if (stop_type == debug_ipc::ExceptionType::kSynthetic) {
      // In every case where GetContinueOp() returns SyntheticStop, this controller should do
      // something. Otherwise there will be an infinite loop since GetContinueOp() will presumably
      // return the same thing given the same conditions.
      //
      // This condition prevents the loop if such a case were to occur. If this assertion hits,
      // GetContinueOp() needs to agree with this function on what to do in the synthetic case.
      FX_NOTREACHED();
      return kStopDone;
    }
    // In the ExceptionType::kNone case, it's normal we didn't do anything if there are no inline
    // routines. This will happen when this controller is used as a sub controller for e.g. the
    // "step over" controller. GetContinueOp() has not been called to classify.
  }

  const Frame* top_frame = stack[0];
  uint64_t ip = top_frame->GetAddress();
  if (current_ranges_.InRange(ip)) {
    Log("In existing range: %s", current_ranges_.ToString().c_str());
    return kContinue;
  }

  Log("Left range: %s", current_ranges_.ToString().c_str());

  if (step_mode_ == StepMode::kSourceLine) {
    // Normally you'll want to use the line information from line_details instead of from the Stack.
    // See big comment below.
    ProcessSymbols* process_symbols = thread()->GetProcess()->GetSymbols();
    LineDetails line_details = process_symbols->LineDetailsForAddress(ip);
    if (FrameFingerprint::Newer(thread()->GetStack().GetFrameFingerprint(0),
                                original_frame_fingerprint_)) {
      // Something changed that should cause us to re-evaluate whether this range needs special
      // handling. We either went from having symbols to not having symbols, or got into a new
      // function.
      FunctionStep func_step = GetFunctionStepAction(thread());
      if (func_step != FunctionStep::kDefault) {
        Log("Got a new function, step mode of %s", FunctionStepToString(func_step));

        // Optimization note: currently this is designed to be very regular so that if we hit a PLT
        // trampoline, we go through it to stop at the actual function and re-evaluate what should
        // happen as if the trampoline didn't exist. But in the "step over" case, we know we'll want
        // to step out of the given function and can omit this step, doing a "step out" directly.
        // The challenge to implementing this is that the code that knows we're going to step out
        // subsequently is at a higher level than we are (it created this object) and this code is
        // already extremely complex.
        //
        // The current design should be fine unless we notice a performance problem with automated
        // stepping in the future. In that case we could short-circuit the PLT stepping and
        // immediately step out in cases where there's no need to know about the function we're
        // stepping to.
        function_step_ = std::make_unique<FunctionThreadController>(func_step);

        // Resume once the function step controller has initialized. This can involve setting
        // breakpoints (for stepping over function prologues) which can asynchronously fail, so
        // don't continue until we know it's OK. Otherwise failures will resume execution without
        // stopping which is not what the user expects.
        //
        // Force the "none" exception type because the current exception won't correspond to the
        // new thread controller's expectations.
        auto resume_async = MakeResumeAsyncThreadCallback(debug_ipc::ExceptionType::kNone);
        function_step_->InitWithThread(thread(), std::move(resume_async.callback));
        return resume_async.ForwardStopOrReturnFuture(function_step_.get(), {});
      }

      // Continue through the default behavior.
      Log("Got into new function with no special handling required.");
    }

    // When stepping by source line the current_ranges_ will be the entry for the current line in
    // the line table. But we could have a line table like this:
    //    line 10  <= current_ranges_
    //    line 11
    //    line 10
    // Initially we were stepping in the range of the first "line 10" entry. But when we leave that,
    // we could have skipped over the "line 11" entry (say for a short-circuited if statement) and
    // could still be on line 10!
    //
    // We could also have "line 0" entries which represent code without any corresponding source
    // line (usually bookkeeping by the compiler). We always want to step over "line 0" code ranges.
    //
    // To make things more complicated, the stack will try to fix up "line 0" locations to use the
    // next real file/line in order to avoid showing "no line information" errors in the stack
    // trace. This means we can't trust the stack frame's location for making stepping decisions and
    // should always use the line_details.
    //
    // This case is a little different than the code in InitWithThread() which always wants to use
    // the stack frame's location if there is ambiguity. This is because when the user starts
    // stepping, they think they're at the location identified by the Stack frame. But once we're in
    // the middle of stepping there is no more expectation about ambiguous stack frames.
    //
    // Note: don't check the original file_line_ variable for line 0 since if the source of the step
    // was in one of these weird locations, all subsequent lines will compare for equality and we'll
    // never stop stepping!
    if (stack.hide_ambiguous_inline_frame_count() > 0) {
      // There are ambiguous locations to step into at this location, the next "step" operation will
      // be to go into that. Clear the range and fall through to the inline stepping code at the
      // bottom of this function.
      //
      // Note in this case the current line_details will normally identify the first line of the
      // most deeply nested inline function, while the current stack frame's location will be the
      // call location of the current inline. This code needs to happen before the line_details are
      // checked because the line_details don't represent the thing we're trying to step.
      current_ranges_ = AddressRanges();
      Log("Stepping hit inline boundary");
    } else if (thread()->GetStack().GetFrameFingerprint(0) == original_frame_fingerprint_ &&
               (line_details.file_line().line() == 0 || line_details.file_line() == *file_line_)) {
      // The frame and file/line matches what we're stepping over. Continue stepping inside the
      // current range.
      current_ranges_ = AddressRanges(line_details.GetExtent());
      Log("Still on the same line, continuing with new range: %s",
          current_ranges_.ToString().c_str());
      return kContinue;
    } else {
      // This "else" case is just that the line information is different than the one we're trying
      // to step over, so we fall through to the "done" code at the end of the function.
      Log("Got to a different line.");
    }
  }

  // Just completed a true step. It may have landed at an ambiguous inline location. When
  // line stepping from an outer frame into a newer inline, always go into exactly one frame. This
  // corresponds to executing instructions on the line before the inline call, and then stopping
  // at the first instruction of the inline call.
  //
  // Need to reset the hide count before doing this because we just stepped *to* the ambiguous
  // location and want to have our default to be to stay in the same (outermost) frame.
  stack.SetHideAmbiguousInlineFrameCount(stack.GetAmbiguousInlineFrameCount());
  TrySteppingIntoInline(StepIntoInline::kCommit);

  // We may have just stepped out to an older frame, issue the return callback if so.
  if (function_return_callback_ &&
      FrameFingerprint::Newer(original_frame_fingerprint_, stack.GetFrameFingerprint(0))) {
    function_return_callback_(return_info_);
  }

  return kStopDone;
}

bool StepThreadController::TrySteppingIntoInline(StepIntoInline command) {
  if (step_mode_ != StepMode::kSourceLine) {
    // Only do inline frame handling when stepping by line.
    //
    // When the user is doing a single-instruction step, ignore special inline frames and always do
    // a real step. The other mode is "address range" which isn't exposed to the user directly so we
    // probably won't encounter it here, but assume that it's also a low-level operation that
    // doesn't need inline handling.
    return false;
  }

  Stack& stack = thread()->GetStack();

  size_t hidden_frame_count = stack.hide_ambiguous_inline_frame_count();
  if (hidden_frame_count == 0) {
    // The Stack object always contains all inline functions nested at the current address. When
    // it's not logically in one or more of them, they will be hidden. Not having any hidden inline
    // frames means there's nothing to a synthetic inline step into.
    return false;
  }

  // Examine the inline frame to potentially unhide.
  const Frame* frame = stack.FrameAtIndexIncludingHiddenInline(hidden_frame_count - 1);
  if (!frame->IsAmbiguousInlineLocation())
    return false;  // No inline or not ambiguous.

  // For "step" to go into an inline function, the line of the inline call must be the same as the
  // line the user was stepping from. This disambiguates these two cases:
  //  1) Stepping on some code followed by an inline call on the same line (should step in).
  //  2) Stepping on a line with no function calls, immediately followed by a different inline
  //     function call on a subsequent line (don't step in).
  // We could get the inline function definition and ask for its file/line. The previous stack
  // frame's file/line will have the same location (the Stack fills this in based on the inline
  // call source). Use the latter to help keep things in sync. This also makes testing easier since
  // the tests don't have to fill in the inline call locations, on the stack.
  const Frame* before_inline_frame = stack.FrameAtIndexIncludingHiddenInline(hidden_frame_count);
  if (before_inline_frame->GetLocation().file_line() != file_line_)
    return false;  // Different lines.

  // Require that the the frame we might step into is newer than the frame we started stepping at.
  // This handles the "step into inline" case.
  //
  // We don't want to do anything when the newer frame is the same level or older than the source.
  // These states indicate that we stepped out of one or more inline frames, and immediately to the
  // beginning of another (or else the location wouldn't be ambiguous). Stepping should leave us
  // at the lower leve of the stack in that case.
  //
  // The Stack object can only get fingerprints for unhidden frames, so unhide it and put it
  // back. Hiding/unhiding is inexpensive so don't worry about it.
  size_t new_hide_count = hidden_frame_count - 1;
  stack.SetHideAmbiguousInlineFrameCount(new_hide_count);
  FrameFingerprint new_inline_fingerprint = stack.GetFrameFingerprint(0);
  stack.SetHideAmbiguousInlineFrameCount(hidden_frame_count);

  // Either the original_frame_fingerprint_ or the new_inline_fingerprint could be null at this
  // point if the CFA for the current frame is 0. This can occur in unsymbolized code and I've also
  // seen it in Go code where it seems the unwinder doesn't completely work.
  //
  // In this case, two null fingerprints will compare equal, and the frame will be considered the
  // same (what we want for this case).
  if (!FrameFingerprint::Newer(new_inline_fingerprint, original_frame_fingerprint_))
    return false;  // Not newer.

  // Inline frame should be stepped into.
  if (command == StepIntoInline::kCommit) {
    stack.SetHideAmbiguousInlineFrameCount(new_hide_count);
    Log("Synthetically stepping into inline frame %s, new hide count = %zu.",
        FrameFunctionNameForLog(stack[0]).c_str(), new_hide_count);
  }
  return true;
}

}  // namespace zxdb
