// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"

namespace zxdb {

StepThreadController::StepThreadController(StepMode mode) : step_mode_(mode) {}
StepThreadController::StepThreadController(AddressRange range)
    : step_mode_(StepMode::kAddressRange), range_(range) {}
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
    range_ = line_details.GetExtent();
  }
  // In the "else" cases, the range will already have been set up.

  cb(Err());
}

ThreadController::ContinueOp StepThreadController::GetContinueOp() {
  if (range_.empty())
    return ContinueOp::StepInstruction();
  return ContinueOp::StepInRange(range_);
}

ThreadController::StopOp StepThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (range_.empty())
    return kStop;  // Single-stepping by instructions, always stop.

  // Only count hardware debug exceptions as being eligable for continuation.
  // We wouldn't want to try to resume from a crash just because it's in our
  // range, or if there was a hardcoded debug instruction in the range, for
  // example.
  if (stop_type != debug_ipc::NotifyException::Type::kHardware)
    return kStop;

  return OnThreadStopIgnoreType(hit_breakpoints);
}

ThreadController::StopOp StepThreadController::OnThreadStopIgnoreType(
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (range_.empty())
    return kStop;  // Single-stepping by instructions, always stop.

  // Most uses of "step in range" will return "stop" here since the program
  // won't prematurely stop while executing a line of code. But the code could
  // crash or there could be a breakpoint in the middle, and those don't
  // count as leaving the range.
  auto frames = thread()->GetFrames();
  FXL_DCHECK(!frames.empty());

  uint64_t ip = frames[0]->GetAddress();
  if (range_.InRange(ip))
    return kContinue;
  return kStop;
}

}  // namespace zxdb
