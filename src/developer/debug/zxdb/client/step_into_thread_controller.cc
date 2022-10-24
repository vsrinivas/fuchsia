// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_into_thread_controller.h"

#include <inttypes.h>

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"

namespace zxdb {

// These constructors all pass the function_return callback directly into the step_into_ controller.
// That would only be issued if we end up stepping *out*, which means there's no prologue and we
// wouldn't encounter a return at any other time.
StepIntoThreadController::StepIntoThreadController(StepMode mode,
                                                   FunctionReturnCallback function_return,
                                                   fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      step_into_(std::make_unique<StepThreadController>(mode, std::move(function_return))) {}

StepIntoThreadController::StepIntoThreadController(const FileLine& line,
                                                   FunctionReturnCallback function_return,
                                                   fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      step_into_(std::make_unique<StepThreadController>(line, std::move(function_return))) {}

StepIntoThreadController::StepIntoThreadController(AddressRanges ranges,
                                                   FunctionReturnCallback function_return,
                                                   fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      step_into_(std::make_unique<StepThreadController>(ranges, std::move(function_return))) {}

StepIntoThreadController::~StepIntoThreadController() = default;

void StepIntoThreadController::InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) {
  SetThread(thread);

  if (thread->GetStack().empty()) {
    cb(Err("Can't step, no frames."));
    return;
  }

  original_frame_fingerprint_ = thread->GetStack().GetFrameFingerprint(0);
  step_into_->InitWithThread(thread, std::move(cb));
}

ThreadController::ContinueOp StepIntoThreadController::GetContinueOp() {
  if (skip_prologue_)
    skip_prologue_->GetContinueOp();
  return step_into_->GetContinueOp();
}

ThreadController::StopOp StepIntoThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  // Once we're doing the skip_prologue operation, it runs until complete and then we're done.
  if (skip_prologue_)
    return skip_prologue_->OnThreadStop(stop_type, hit_breakpoints);

  // Handle normal low-level "step into".
  StopOp op = step_into_->OnThreadStop(stop_type, hit_breakpoints);
  if (op != kStopDone)
    return op;

  if (!should_skip_prologue_)
    return kStopDone;  // Don't need to do anything on top of the normal step.

  // If we get here the step controller thinks it's done. If we're not in a prologue now, we're
  // done. Otherwise we need to step through the prologue.

  // We can only be in a prologue if we've stepped into a new physical frame.
  //
  // This check is unnecessary as the symbol lookup below should handle all cases since stepping by
  // line should never leave you in a function prologue that's not a new frame. But most of the time
  // we're stepping in the same frame and a symbol lookup is relatively heavyweight. This is a nice
  // filter before doing the full lookup.
  const Stack& stack = thread()->GetStack();
  if (stack[0]->IsInline())
    return kStopDone;  // Inline frames don't have prologues.
  if (!FrameFingerprint::Newer(stack.GetFrameFingerprint(0), original_frame_fingerprint_))
    return kStopDone;  // Not in a newer frame, no prologue to skip.

  if (stack.empty()) {
    FX_NOTREACHED();  // Should always have a current frame on stop.
    return kUnexpected;
  }
  uint64_t current_ip = stack[0]->GetAddress();

  // Symbolize the current address and ask to skip the prologue. This will automatically adjust the
  // resulting address to be after the prologue if there is one.
  ResolveOptions resolve_options;
  resolve_options.symbolize = true;
  resolve_options.skip_function_prologue = true;
  std::vector<Location> symbolized_locs =
      thread()->GetProcess()->GetSymbols()->ResolveInputLocation(InputLocation(current_ip),
                                                                 resolve_options);
  FX_DCHECK(symbolized_locs.size() == 1);  // Should always return one match.

  const Location& after_prologue = symbolized_locs[0];
  if (current_ip == after_prologue.address()) {
    Log("Not in a function prologue, stopping.");
    return kStopDone;
  }

  // Got to a prologue, now step to the end. This uses a "step over" controller since sometimes
  // there can be function calls in the prologue itself. We want to automatically skip these.
  // Normally they are bookkeeping functions (for example, asan injects "stack malloc" calls there)
  // that the user does not want to stop at.
  Log("Stepped to function prologue ending at 0x%" PRIx64 ". Going over it.",
      after_prologue.address());
  skip_prologue_ = std::make_unique<StepOverThreadController>(
      AddressRanges(AddressRange(current_ip, after_prologue.address())));
  // Init for this object is guaranteed synchronous so we don't have to wait for the callback.
  skip_prologue_->InitWithThread(thread(), [](const Err&) {});

  // Don't pass the exception type or breakpoints to the new controller. Depending on how we got
  // here, the exception type may not match what the step controller expects. it just needs to know
  // that execution stopped.
  return skip_prologue_->OnThreadStop(debug_ipc::ExceptionType::kNone, {});
}

}  // namespace zxdb
