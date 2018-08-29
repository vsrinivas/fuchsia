// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_into_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/step_in_range_thread_controller.h"
#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"

namespace zxdb {

StepIntoThreadController::StepIntoThreadController() {}
StepIntoThreadController::~StepIntoThreadController() {}

void StepIntoThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  auto frames = thread->GetFrames();
  FXL_DCHECK(!frames.empty());

  LineDetails line_details =
      thread->GetProcess()->GetSymbols()->LineDetailsForAddress(
          frames[0]->GetAddress());
  if (!line_details.entries().empty()) {
    step_in_range_ = std::make_unique<StepInRangeThreadController>(
        line_details.entries()[0].range.begin(),
        line_details.entries().back().range.end());
    step_in_range_->InitWithThread(thread, std::move(cb));
  } else {
    // The "else" case is that step_in_range_ remains null and GetContinueOp()
    // will fall back on single-stepping by instruction.
    cb(Err());
  }
}

ThreadController::ContinueOp StepIntoThreadController::GetContinueOp() {
  if (step_in_range_)
    return step_in_range_->GetContinueOp();

  // Fall back on single-stepping by instructions if no range could be
  // computed.
  return ContinueOp::StepInstruction();
}

ThreadController::StopOp StepIntoThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (step_in_range_)
    return step_in_range_->OnThreadStop(stop_type, hit_breakpoints);

  // When single-stepping by instructions, always stop.
  return kStop;
}

}  // namespace zxdb
