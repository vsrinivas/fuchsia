// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_in_range_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"

namespace zxdb {

StepInRangeThreadController::StepInRangeThreadController(uint64_t begin,
                                                         uint64_t end)
    : begin_(begin), end_(end) {}

StepInRangeThreadController::~StepInRangeThreadController() = default;

void StepInRangeThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);
  cb(Err());
}

ThreadController::ContinueOp StepInRangeThreadController::GetContinueOp() {
  return ContinueOp::StepInRange(begin_, end_);
}

ThreadController::StopOp StepInRangeThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  // Most uses of "step in range" will return "stop" here since the program
  // won't prematurely stop while executing a line of code. But the code could
  // crash or there could be a breakpoint in the middle, and those don't
  // count as leaving the range.
  auto frames = thread()->GetFrames();
  FXL_DCHECK(!frames.empty());

  // Only count hardware debug exceptions in the range as being eligable for
  // continuation. We wouldn't want to try to resume from a crash just because
  // it's in our range, or if there was a hardcoded debug instruction in the
  // range, for example. This controller single steps which always generates
  // hardware debug exceptions.
  uint64_t ip = frames[0]->GetAddress();
  if (stop_type == debug_ipc::NotifyException::Type::kHardware &&
      ip >= begin_ && ip < end_)
    return kContinue;
  return kStop;
}

}  // namespace zxdb
