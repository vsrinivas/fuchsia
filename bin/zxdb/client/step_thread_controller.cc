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
StepThreadController::StepThreadController(AddressRange range)
    : step_mode_(StepMode::kAddressRange), current_range_(range) {}
StepThreadController::~StepThreadController() = default;

void StepThreadController::InitWithThread(Thread* thread,
                                          std::function<void(const Err&)> cb) {
  set_thread(thread);

  auto frames = thread->GetFrames();
  FXL_DCHECK(!frames.empty());
  uint64_t ip = frames[0]->GetAddress();

  if (step_mode_ == StepMode::kSourceLine) {
    LineDetails line_details =
        thread->GetProcess()->GetSymbols()->LineDetailsForAddress(ip);
    file_line_ = line_details.file_line();
    current_range_ = line_details.GetExtent();

    original_frame_fingerprint_ = thread->GetFrameFingerprint(0);

    Log("Stepping in %s:%d [0x%" PRIx64 ", 0x%" PRIx64 ")",
        file_line_.file().c_str(), file_line_.line(), current_range_.begin(),
        current_range_.end());
  } else {
    // In the "else" cases, the range will already have been set up.
    Log("Stepping in [0x%" PRIx64 ", 0x%" PRIx64 ")", current_range_.begin(),
        current_range_.end());
  }

  cb(Err());
}

ThreadController::ContinueOp StepThreadController::GetContinueOp() {
  if (finish_unsymolized_function_)
    return finish_unsymolized_function_->GetContinueOp();
  if (current_range_.empty())
    return ContinueOp::StepInstruction();
  return ContinueOp::StepInRange(current_range_);
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
    // Only count hardware debug exceptions as being eligable for continuation.
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

  // Most uses of "step in range" will return "stop" here since the program
  // won't prematurely stop while executing a line of code. But the code could
  // crash or there could be a breakpoint in the middle, and those don't
  // count as leaving the range.
  auto frames = thread()->GetFrames();
  FXL_DCHECK(!frames.empty());

  uint64_t ip = frames[0]->GetAddress();
  if (current_range_.InRange(ip)) {
    Log("In existing range: [0x%" PRIx64 ", 0x%" PRIx64 ")",
        current_range_.begin(), current_range_.end());
    return kContinue;
  }

  Log("Left range: [0x%" PRIx64 ", 0x%" PRIx64 ")", current_range_.begin(),
      current_range_.end());

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
        // LLVM indexes ELF imports in the symbol database (type
        // eSymbolTypeTrampoline) and can then compare to see if the current
        // code is a trampoline. See
        // DynamicLoaderPOSIXDYLD::GetStepThroughTrampolinePlan.
        //
        // We should do something similar which will be less prone to errors.
        // GDB does something similar but also checks that the instruction is
        // the right type of jump. This involves two memory lookups which make
        // it difficult for us to implement since they require async calls.
        // We might be able to just check that the address is inside the
        // prodecure linkage table (see below).
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
        current_range_ = AddressRange();  // No range = step by instruction.
        return kContinue;
      } else if (FrameFingerprint::Newer(thread()->GetFrameFingerprint(0),
                                         original_frame_fingerprint_)) {
        // Called a new stack frame that has no symbols. We need to "finish" to
        // step over the unsymbolized code to automatically step over the
        // unsymbolized code.
        Log("Called unsymbolized function, stepping out.");
        FXL_DCHECK(original_frame_fingerprint_.is_valid());
        finish_unsymolized_function_ = std::make_unique<FinishThreadController>(
            FinishThreadController::ToFrame(), frames[1]->GetAddress(),
            original_frame_fingerprint_);
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

    // When stepping by source line the current_range_ will be the entry for
    // the current line in the line table. But we could have a line table
    // like this:
    //    line 10  <= current_range_
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
      current_range_ = line_details.GetExtent();
      Log("Got new range for line: [0x%" PRIx64 ", 0x%" PRIx64 ")",
          current_range_.begin(), current_range_.end());
      return kContinue;
    }
  }

  return kStop;
}

}  // namespace zxdb
