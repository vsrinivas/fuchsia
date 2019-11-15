// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_thread_controller.h"

#include <inttypes.h>

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

StepThreadController::StepThreadController(StepMode mode) : step_mode_(mode) {}

StepThreadController::StepThreadController(const FileLine& line)
    : step_mode_(StepMode::kSourceLine), file_line_(line) {}

StepThreadController::StepThreadController(AddressRanges ranges)
    : step_mode_(StepMode::kAddressRange), current_ranges_(ranges) {}

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
      // Always take the file/line from the stack rather than the line table.
      // The stack will have been fixed up and may reference the calling line
      // for an inline routine, while the line table will reference the inlined
      // source that generated the instructions.
      file_line_ = top_frame->GetLocation().file_line();
    }

    LineDetails line_details = thread->GetProcess()->GetSymbols()->LineDetailsForAddress(ip);
    if (line_details.file_line() == *file_line_) {
      // When the stack and the line details match up, the range from the line
      // table is usable.
      current_ranges_ = AddressRanges(line_details.GetExtent());
      Log("Stepping in %s:%d %s", file_line_->file().c_str(), file_line_->line(),
          current_ranges_.ToString().c_str());
    } else {
      // Otherwise keep the current range empty to cause a step into inline
      // routine or potentially a single step.
      current_ranges_ = AddressRanges();
      Log("Stepping in empty range.");
    }

    original_frame_fingerprint_ = thread->GetStack().GetFrameFingerprint(0);
  } else {
    // In the "else" cases, the range will already have been set up.
    Log("Stepping in %s", current_ranges_.ToString().c_str());
  }

  cb(Err());
}

ThreadController::ContinueOp StepThreadController::GetContinueOp() {
  if (finish_unsymolized_function_)
    return finish_unsymolized_function_->GetContinueOp();

  // The stack shouldn't be empty when stepping in a range, but in case it is,
  // fall back to single-step.
  const auto& stack = thread()->GetStack();
  if (stack.empty())
    return ContinueOp::StepInstruction();

  // Check for inlines. This case will likely have an empty address range so
  // the inline check needs to be done before checking for empty ranges below.
  //
  // GetContinueOp() should not modify thread state, so we need to return
  // whether we want to modify the inline stack. Returning SyntheticStop here
  // will schedule a call OnThreadStop with a synthetic exception. The inline
  // stack should actually be modified at that point.
  if (TrySteppingIntoInline(StepIntoInline::kQuery))
    return ContinueOp::SyntheticStop();

  // An empty range means to step by instruction.
  if (current_ranges_.empty())
    return ContinueOp::StepInstruction();

  // Use the IP from the top of the stack to figure out which range to send
  // to the agent (it only accepts one, while we can have a set).
  if (auto inside = current_ranges_.GetRangeContaining(stack[0]->GetAddress()))
    return ContinueOp::StepInRange(*inside);

  // Don't generally expect to be continuing in a range that we're not
  // currently inside of. But it could be the caller is expecting the next
  // instruction to be in that range, so fall back to single-step mode.
  return ContinueOp::StepInstruction();
}

ThreadController::StopOp StepThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (finish_unsymolized_function_) {
    Log("Trying to step out of unsymbolized function.");
    if (finish_unsymolized_function_->OnThreadStop(stop_type, hit_breakpoints) == kContinue) {
      finish_unsymolized_function_->Log("Reported continue.");
      return kContinue;
    }

    finish_unsymolized_function_->Log("Reported stop, continuing with step.");
    finish_unsymolized_function_.reset();
  } else {
    // The only real exception type we care about (as opposed to synthetic and
    // "none" -- see below) are the single step exceptions. We wouldn't want to
    // try to resume from a crash just because it's in our range, or if there
    // was a hardcoded debug instruction in the range, for example.
    //
    // This must happen only when there's no "finish" controller since a
    // successful "finish" hit will have a software breakpoint.
    //
    // A "none" type means to ignore the exception type and evaluate the
    // current code location. It is used when this controller is nested. A
    // synthetic exception is used to step into inline functions.
    if ((stop_type != debug_ipc::ExceptionType::kNone &&
         stop_type != debug_ipc::ExceptionType::kSynthetic &&
         stop_type != debug_ipc::ExceptionType::kSingleStep) ||
        !hit_breakpoints.empty()) {
      Log("Not our exception type, stop is somebody else's.");
      return kUnexpected;
    }
  }

  Stack& stack = thread()->GetStack();
  if (stack.empty())
    return kUnexpected;  // Agent sent bad state, give up trying to step.

  const Frame* top_frame = stack[0];
  uint64_t ip = top_frame->GetAddress();
  if (current_ranges_.InRange(ip)) {
    Log("In existing range: %s", current_ranges_.ToString().c_str());
    return kContinue;
  }

  Log("Left range: %s", current_ranges_.ToString().c_str());

  if (step_mode_ == StepMode::kSourceLine) {
    ProcessSymbols* process_symbols = thread()->GetProcess()->GetSymbols();
    LineDetails line_details = process_symbols->LineDetailsForAddress(ip);

    if (!line_details.is_valid()) {
      // Stepping by line but we ended up in a place where there's no line
      // information.
      if (stop_on_no_symbols_) {
        Log("Stopping because there are no symbols.");
        return kStopDone;
      }
      return OnThreadStopOnUnsymbolizedCode();
    }

    // When stepping by source line the current_ranges_ will be the entry for
    // the current line in the line table. But we could have a line table
    // like this:
    //    line 10  <= current_ranges_
    //    line 11
    //    line 10
    // Initially we were stepping in the range of the first "line 10" entry.
    // But when we leave that, we could have skipped over the "line 11" entry
    // (say for a short-circuited if statement) and could still be on line 10!
    //
    // We could also have "line 0" entries which represent code without any
    // corresponding source line (usually bookkeeping by the compiler).
    //
    // This checks if we're in another entry representing the same source line
    // or line 0, and continues stepping in that range.
    //
    // Note: don't check the original file_line_ variable for line 0 since if
    // the source of the step was in one of these weird locations, all
    // subsequent lines will compare for equality and we'll never stop
    // stepping!
    //
    // As in InitWithThread(), always use the stack's file/line over the result
    // from the line table.
    const Location& top_location = top_frame->GetLocation();
    if (top_location.file_line().line() == 0 || top_location.file_line() == *file_line_) {
      // Still on the same line.
      if (top_location.file_line() == line_details.file_line()) {
        // Can use the range from the line table.
        current_ranges_ = AddressRanges(line_details.GetExtent());
        Log("Got new range for line: %s", current_ranges_.ToString().c_str());
        return kContinue;
      } else {
        // Line table and stack don't match due to inlined calls. Clearing the
        // range will make the next operation will either single-step or step
        // into an inline function.
        current_ranges_ = AddressRanges();
        // Fall-through to trying to fixup inline frames or stopping.
      }
    }
  }

  if (stop_type == debug_ipc::ExceptionType::kSynthetic ||
      stop_type == debug_ipc::ExceptionType::kNone) {
    // Handle virtually stepping into inline functions by modifying the hidden
    // ambiguous inline frame count.
    //
    // This should only happen for synthetic stops because modifying the hide
    // count is an alternative to actually stepping the CPU. Doing this after a
    // real step will modify the stack for the *next* instruction (like doing
    // "step into" twice in the case of ambiguous inline frames).
    //
    // On the other hand, this check should happen after the other types of
    // range checking in case the thread is still in range.
    if (TrySteppingIntoInline(StepIntoInline::kCommit))
      return kStopDone;
  } else {
    // Just completed a true step. It may have landed at an ambiguous inline location. When
    // line stepping from an outer frame into a newer inline, always go into exactly one frame. This
    // corresponds to executing instructions on the line before the inline call, and then stopping
    // at the first instruction of the inline call.
    //
    // Need to reset the hide count before doing this because we just stepped *to* the ambiguous
    // location and want to have our default to be to stay in the same (outermost) frame.
    stack.SetHideAmbiguousInlineFrameCount(stack.GetAmbiguousInlineFrameCount());
    TrySteppingIntoInline(StepIntoInline::kCommit);
  }
  return kStopDone;
}

bool StepThreadController::TrySteppingIntoInline(StepIntoInline command) {
  if (step_mode_ != StepMode::kSourceLine) {
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

  FXL_DCHECK(original_frame_fingerprint_.is_valid());  // Should have been filled in previously.
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

ThreadController::StopOp StepThreadController::OnThreadStopOnUnsymbolizedCode() {
  Log("Stepped into code with no symbols.");

  const Stack& stack = thread()->GetStack();
  const Frame* top_frame = stack[0];

  ProcessSymbols* process_symbols = thread()->GetProcess()->GetSymbols();
  if (process_symbols->HaveSymbolsLoadedForModuleAt(top_frame->GetAddress())) {
    // We ended up in code with no symbols inside a module where we expect
    // to have symbols. The common cause of this is a shared library thunk:
    // When there is an imported symbol, all code in a module will jump to
    // some generated code (no symbols) that in turn does an indirect jump
    // to the destination. The destination of the indirect jump is what's
    // filled in by the dynamic loader when imports are resolved.
    //
    // LLDB indexes ELF imports in the symbol database (type
    // eSymbolTypeTrampoline) and can then compare to see if the current
    // code is a trampoline. See
    // DynamicLoaderPOSIXDYLD::GetStepThroughTrampolinePlan.
    //
    // We should do something similar which will be less prone to errors.
    // GDB does something similar but also checks that the instruction is
    // the right type of jump. This involves two memory lookups which make
    // it difficult for us to implement since they require async calls.
    // We might be able to just check that the address is inside the
    // procedure linkage table (see below).
    //
    // ELF imports
    // -----------
    // ELF imports go through the "procedure linkage table" (see the ELF
    // spec) which allows lazy resolution. These trampolines have a default
    // jump address is to the next instruction which then pushes the item
    // index on the stack and does a dance to jump to the dynamic linker to
    // resolve this import. Once resolved, the first jump takes the code
    // directly to the destination.
    //
    // Our loader seems to resolve these up-front. In the future we might
    // need to add logic to step over the dynamic loader when its resolving
    // the import.
    Log("In function with no symbols, single-stepping.");
    current_ranges_ = AddressRanges();  // No range: step by instruction.
    return kContinue;
  }

  if (FrameFingerprint::Newer(thread()->GetStack().GetFrameFingerprint(0),
                              original_frame_fingerprint_)) {
    // Called a new stack frame that has no symbols. We need to "finish" to
    // step over the unsymbolized code to automatically step over the
    // unsymbolized code.
    Log("Called unsymbolized function, stepping out.");
    FXL_DCHECK(original_frame_fingerprint_.is_valid());
    finish_unsymolized_function_ =
        std::make_unique<FinishThreadController>(thread()->GetStack(), 0);
    finish_unsymolized_function_->InitWithThread(thread(), [](const Err&) {});
    return kContinue;
  }

  // Here we jumped (not called, we checked the frames above) to some
  // unsymbolized code. Don't know what this is so stop.
  Log("Jumped to unsymbolized code, giving up and stopping.");
  return kStopDone;
}

}  // namespace zxdb
