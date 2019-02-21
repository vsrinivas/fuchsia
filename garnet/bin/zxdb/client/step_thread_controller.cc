// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_thread_controller.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/finish_thread_controller.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/symbols/line_details.h"
#include "garnet/bin/zxdb/symbols/process_symbols.h"

namespace zxdb {

StepThreadController::StepThreadController(StepMode mode) : step_mode_(mode) {}
StepThreadController::StepThreadController(AddressRanges ranges)
    : step_mode_(StepMode::kAddressRange), current_ranges_(ranges) {}
StepThreadController::~StepThreadController() = default;

void StepThreadController::InitWithThread(Thread* thread,
                                          std::function<void(const Err&)> cb) {
  set_thread(thread);

  const Stack& stack = thread->GetStack();
  if (stack.empty()) {
    cb(Err("Can't step, no frames."));
    return;
  }
  uint64_t ip = stack[0]->GetAddress();

  if (step_mode_ == StepMode::kSourceLine) {
    LineDetails line_details =
        thread->GetProcess()->GetSymbols()->LineDetailsForAddress(ip);
    file_line_ = line_details.file_line();
    current_ranges_ = AddressRanges(line_details.GetExtent());

    original_frame_fingerprint_ = *thread->GetStack().GetFrameFingerprint(0);

    Log("Stepping in %s:%d %s", file_line_.file().c_str(), file_line_.line(),
        current_ranges_.ToString().c_str());
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
  if (current_ranges_.empty() || stack.empty())
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
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (finish_unsymolized_function_) {
    Log("Trying to step out of unsymbolized function.");
    if (finish_unsymolized_function_->OnThreadStop(
            stop_type, hit_breakpoints) == kContinue) {
      finish_unsymolized_function_->Log("Reported continue.");
      return kContinue;
    }

    finish_unsymolized_function_->Log("Reported stop, continuing with step.");
    finish_unsymolized_function_.reset();
  } else {
    // Only count hardware debug exceptions as being eligible for continuation.
    // We wouldn't want to try to resume from a crash just because it's in our
    // range, or if there was a hardcoded debug instruction in the range, for
    // example.
    //
    // This must happen only when there's no "finish" controller since a
    // successful "finish" hit will have a software breakpoint.
    if (stop_type != debug_ipc::NotifyException::Type::kSingleStep)
      return kStop;
  }

  return OnThreadStopIgnoreType(hit_breakpoints);
}

ThreadController::StopOp StepThreadController::OnThreadStopIgnoreType(
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  // We shouldn't have a "finish" sub controller at this point. It needs the
  // stop type to detect when it's hit, so we can't call it from here.
  //
  // This function is called directly when "Step" is used as a sub-controller
  // and the thread stopped for another reason (like a higher-priority
  // controller). We could only get here with a "finish" operation pending if
  // the parent controller interrupted us even though we're saying "continue"
  // to do some other kind of sub-controller, and then got back to us (if we
  // created a sub-controller and haven't deleted it yet, we've only ever said
  // "continue"). Currently that never happens.
  //
  // If we do legitimately need to support this case in the future,
  // FinishThreadController would also need an OnThreadStopIgnoreType()
  // function that we call from here.
  FXL_DCHECK(!finish_unsymolized_function_);

  Stack& stack = thread()->GetStack();
  if (stack.empty())
    return kStop;  // Agent sent bad state, give up trying to step.

  uint64_t ip = stack[0]->GetAddress();
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
        return kStop;
      }

      Log("Stepped into code with no symbols.");
      if (process_symbols->HaveSymbolsLoadedForModuleAt(ip)) {
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
      } else if (FrameFingerprint::Newer(
                     *thread()->GetStack().GetFrameFingerprint(0),
                     original_frame_fingerprint_)) {
        // Called a new stack frame that has no symbols. We need to "finish" to
        // step over the unsymbolized code to automatically step over the
        // unsymbolized code.
        Log("Called unsymbolized function, stepping out.");
        FXL_DCHECK(original_frame_fingerprint_.is_valid());
        finish_unsymolized_function_ =
            std::make_unique<FinishThreadController>(stack, 0);
        finish_unsymolized_function_->InitWithThread(thread(),
                                                     [](const Err&) {});
        return kContinue;
      } else {
        // Here we jumped (not called, we checked the frames above) to some
        // unsymbolized code. Don't know what this is so stop.
        Log("Jumped to unsymbolized code, giving up and stopping.");
        return kStop;
      }
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
    if (line_details.file_line().line() == 0 ||
        line_details.file_line() == file_line_) {
      current_ranges_ = AddressRanges(line_details.GetExtent());
      Log("Got new range for line: %s", current_ranges_.ToString().c_str());
      return kContinue;
    }
  }

  // Normal stop. When stepping has resulted in landing at an ambiguous inline
  // location, always consider the location to be the oldest frame to allow the
  // user to step into the inline frames if desired.
  //
  // We don't want to select the same frame here that we were originally
  // stepping in because we could have just stepped out of a frame to an inline
  // function starting immediately after the call. We always want to at the
  // oldest possible inline call.
  stack.SetHideAmbiguousInlineFrameCount(stack.GetAmbiguousInlineFrameCount());
  return kStop;
}

}  // namespace zxdb
