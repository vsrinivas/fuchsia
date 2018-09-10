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
  }
  // In the "else" cases, the range will already have been set up.

  cb(Err());
}

ThreadController::ContinueOp StepThreadController::GetContinueOp() {
  if (current_range_.empty())
    return ContinueOp::StepInstruction();
  return ContinueOp::StepInRange(current_range_);
}

ThreadController::StopOp StepThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (current_range_.empty())
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
  if (current_range_.empty())
    return kStop;  // Single-stepping by instructions, always stop.

  // Most uses of "step in range" will return "stop" here since the program
  // won't prematurely stop while executing a line of code. But the code could
  // crash or there could be a breakpoint in the middle, and those don't
  // count as leaving the range.
  auto frames = thread()->GetFrames();
  FXL_DCHECK(!frames.empty());

  uint64_t ip = frames[0]->GetAddress();
  if (current_range_.InRange(ip))
    return kContinue;  // In existing range, can continue.

  if (step_mode_ == StepMode::kSourceLine) {
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
    LineDetails line_details =
        thread()->GetProcess()->GetSymbols()->LineDetailsForAddress(ip);
    if (line_details.is_valid() && (line_details.file_line().line() == 0 ||
        line_details.file_line() == file_line_)) {
      current_range_ = line_details.GetExtent();
      return kContinue;
    }
  }

  return kStop;
}

}  // namespace zxdb
