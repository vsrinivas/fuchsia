// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_over_thread_controller.h"

#include "garnet/bin/zxdb/client/finish_thread_controller.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/step_in_range_thread_controller.h"
#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"
#include "lib/fxl/logging.h"

namespace zxdb {

StepOverThreadController::StepOverThreadController(ConstructionMode mode)
    : construction_mode_(mode) {
  FXL_DCHECK(mode != kAddressRange);
}

StepOverThreadController::StepOverThreadController(uint64_t begin, uint64_t end)
    : construction_mode_(kAddressRange),
      step_in_range_(
          std::make_unique<StepInRangeThreadController>(begin, end)) {}

StepOverThreadController::~StepOverThreadController() = default;

void StepOverThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  // Save the info for the frame we're stepping in.
  frame_fingerprint_ = thread->GetFrameFingerprint(0);

  auto frames = thread->GetFrames();
  uint64_t ip = frames[0]->GetAddress();
  switch (construction_mode_) {
    case kAddressRange: {
      FXL_DCHECK(step_in_range_.get());  // Should have been made in ctor.
      break;
    }
    case kSourceLine: {
      LineDetails line_details =
          thread->GetProcess()->GetSymbols()->LineDetailsForAddress(ip);
      if (!line_details.entries().empty()) {
        step_in_range_ = std::make_unique<StepInRangeThreadController>(
            line_details.entries()[0].range.begin(),
            line_details.entries().back().range.end());
      } else {
        // No source information, fall back to instruction-based.
        step_in_range_ =
            std::make_unique<StepInRangeThreadController>(ip, ip + 1);
      }
      break;
    }
    case kInstruction: {
      // To step a single instruction, make a range of one byte to step in.
      step_in_range_ =
          std::make_unique<StepInRangeThreadController>(ip, ip + 1);
      break;
    }
  }

  step_in_range_->InitWithThread(thread, std::move(cb));
}

ThreadController::ContinueOp StepOverThreadController::GetContinueOp() {
  if (finish_)
    return finish_->GetContinueOp();
  return step_in_range_->GetContinueOp();
}

ThreadController::StopOp StepOverThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (finish_) {
    // Currently trying to step out of a sub-frame.
    if (finish_->OnThreadStop(stop_type, hit_breakpoints) == kContinue)
      return kContinue;  // Not done stepping out, keep working on it.

    // Done stepping out. The "finish" operation is complete, but we may need
    // to resume single-stepping in the outer frame.
    finish_.reset();
  }

  if (step_in_range_->OnThreadStop(stop_type, hit_breakpoints) == kContinue)
    return kContinue;  // Still in range, keep stepping.

  // If we get here the thread is no longer in range but could be in a sub-
  // frame that we need to step out of.
  FrameFingerprint current_fingerprint = thread()->GetFrameFingerprint(0);
  if (!FrameFingerprint::Newer(current_fingerprint, frame_fingerprint_))
    return kStop;  // Not a new frame.

  auto frames = thread()->GetFrames();
  if (frames.size() < 2)
    return kStop;  // Not enough frames to step out.

  // Begin stepping out of the sub-frame. The "finish" command initialization
  // is technically asynchronous since it's waiting for the breakpoint to be
  // successfully set. Since we're supplying an address to run to instead of a
  // symbol, there isn't much that can go wrong other than the process could
  // be terminated out from under us or the memory is unmapped.
  //
  // These cases are catastrophic anyway so don't worry about those errors.
  // Waiting for a full round-trip to the debugged system for every function
  // call in a "next" command would slow everything down and make things
  // more complex. It also means that the thread may be stopped if the user
  // asks for the state in the middle of a "next" command which would be
  // surprising.
  //
  // Since the IPC will serialize the command, we know that successful
  // breakpoint sets will arrive before telling the thread to continue.
  finish_ = std::make_unique<FinishThreadController>(
      FinishThreadController::ToFrame(), frames[1]->GetAddress(),
      frame_fingerprint_);
  finish_->InitWithThread(thread(), [](const Err&) {});
  return finish_->OnThreadStop(stop_type, hit_breakpoints);
}

}  // namespace zxdb
