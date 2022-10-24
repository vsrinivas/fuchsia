// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_into_specific_thread_controller.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/step_into_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

StepIntoSpecificThreadController::StepIntoSpecificThreadController(AddressRange over_range,
                                                                   fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
      into_address_(over_range.end()),
      step_over_(std::make_unique<StepOverThreadController>(AddressRanges(over_range))) {}

StepIntoSpecificThreadController::~StepIntoSpecificThreadController() = default;

void StepIntoSpecificThreadController::InitWithThread(Thread* thread,
                                                      fit::callback<void(const Err&)> cb) {
  SetThread(thread);
  step_over_->InitWithThread(thread, std::move(cb));
}

ThreadController::ContinueOp StepIntoSpecificThreadController::GetContinueOp() {
  if (step_over_)
    return step_over_->GetContinueOp();
  if (step_into_)
    return step_into_->GetContinueOp();

  FX_NOTREACHED();  // Should not be continuing from this state.
  return ContinueOp::Continue();
}

ThreadController::StopOp StepIntoSpecificThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (step_over_) {
    if (auto op = step_over_->OnThreadStop(stop_type, hit_breakpoints); op != kStopDone)
      return op;

    // Step over phase complete.
    step_over_.reset();

    // Validate our current location before doing the "step into".
    TargetPointer ip = thread()->GetStack()[0]->GetAddress();
    if (ip != into_address_) {
      Log("Stepped outside of our range, skipping 'step into'.");
      return kStopDone;
    }

    Log("Step over complete, now stepping into.");
    step_into_ = std::make_unique<StepIntoThreadController>(StepMode::kSourceLine);
    step_into_->InitWithThread(thread(), [](const Err&) {});
    return kContinue;
  }

  if (step_into_)
    return step_into_->OnThreadStop(stop_type, hit_breakpoints);

  FX_NOTREACHED();  // Should have reported "done" if we skipped the step into.
  return kStopDone;
}

}  // namespace zxdb
