// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/until_thread_controller.h"

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

UntilThreadController::UntilThreadController(std::vector<InputLocation> locations)
    : ThreadController(), locations_(std::move(locations)), weak_factory_(this) {}

UntilThreadController::UntilThreadController(std::vector<InputLocation> locations,
                                             FrameFingerprint newest_frame, FrameComparison cmp)
    : ThreadController(),
      locations_(std::move(locations)),
      threshold_frame_(newest_frame),
      comparison_(cmp),
      weak_factory_(this) {}

UntilThreadController::~UntilThreadController() {
  if (breakpoint_)
    GetSystem()->DeleteBreakpoint(breakpoint_.get());
}

void UntilThreadController::InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) {
  SetThread(thread);

  BreakpointSettings settings;
  settings.scope = ExecutionScope(thread);
  settings.locations = std::move(locations_);

  // Frame-tied triggers can't be one-shot because we need to check the stack every time it
  // triggers. In the non-frame case the one-shot breakpoint will be slightly more efficient.
  settings.one_shot = !threshold_frame_.is_valid();

  breakpoint_ = GetSystem()->CreateNewInternalBreakpoint()->GetWeakPtr();
  // The breakpoint may post the callback asynchronously, so we can't be sure this class is still
  // alive when this callback is issued, even though we destroy the breakpoint in the destructor.
  breakpoint_->SetSettings(settings, [weak_this = weak_factory_.GetWeakPtr(),
                                      cb = std::move(cb)](const Err& err) mutable {
    if (weak_this)
      weak_this->OnBreakpointSet(err, std::move(cb));
  });
}

ThreadController::ContinueOp UntilThreadController::GetContinueOp() {
  // Stopping the thread is done via a breakpoint, so the thread can always be resumed with no
  // qualifications.
  return ContinueOp::Continue();
}

ThreadController::StopOp UntilThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  // Other controllers such as the StepOverRangeThreadController can use this as a sub-controller.
  // If the controllers don't care about breakpoint set failures, they may start using the thread
  // right away without waiting for the callback in InitWithThread() to asynchronously complete
  // (indicating the breakpoint was set successful).
  //
  // This is generally fine, we just need to be careful not to do anything in OnBreakpointSet() that
  // the code in this function depends on.
  if (!breakpoint_) {
    // Our internal breakpoint shouldn't be deleted out from under ourselves.
    FXL_NOTREACHED();
    return kUnexpected;
  }

  // Only care about stops if one of the breakpoints hit was ours. Don't check the stop_type since
  // as long as the breakpoint was hit, we don't care how the program got there (it could have
  // single-stepped to the breakpoint).
  Breakpoint* our_breakpoint = breakpoint_.get();
  bool is_our_breakpoint = true;
  for (auto& hit : hit_breakpoints) {
    if (hit && hit.get() == our_breakpoint) {
      is_our_breakpoint = true;
      break;
    }
  }
  if (!is_our_breakpoint) {
    Log("Not our breakpoint.");
    return kUnexpected;
  }

  if (!threshold_frame_.is_valid()) {
    Log("No frame check required.");
    return kStopDone;
  }

  const Stack& stack = thread()->GetStack();
  if (stack.empty()) {
    FXL_NOTREACHED();  // Should always have a current frame on stop.
    return kUnexpected;
  }

  // If inline frames are ambiguous and the one we want is one of the ambiguous ones, use it.
  if (comparison_ == kRunUntilEqualOrOlderFrame)
    SetInlineFrameIfAmbiguous(InlineFrameIs::kEqual, threshold_frame_);
  else
    SetInlineFrameIfAmbiguous(InlineFrameIs::kOneBefore, threshold_frame_);

  // Check frames.
  FrameFingerprint current_frame = stack.GetFrameFingerprint(0);
  if (FrameFingerprint::Newer(current_frame, threshold_frame_)) {
    Log("In newer frame, ignoring.");
    return kContinue;
  }
  if (comparison_ == kRunUntilOlderFrame && current_frame == threshold_frame_) {
    // In kRunUntilOlderFrame mode, the threshold frame fingerprint itself is one that should
    // continue running.
    Log("In threshold frame, ignoring.");
    return kContinue;
  }
  Log("Found target frame (or older).");
  return kStopDone;
}

System* UntilThreadController::GetSystem() { return &thread()->session()->system(); }

Target* UntilThreadController::GetTarget() { return thread()->GetProcess()->GetTarget(); }

void UntilThreadController::OnBreakpointSet(const Err& err, fit::callback<void(const Err&)> cb) {
  // This may get called after the thread stop in some cases so don't do anything important in this
  // function. See OnThreadStop().
  if (err.has_error()) {
    // Breakpoint setting failed.
    cb(err);
  } else if (!breakpoint_ || breakpoint_->GetLocations().empty()) {
    // Setting the breakpoint may have resolved to no locations and the breakpoint is now pending.
    // For "until" this is not good because if the user does "until SomethingNonexistant" they would
    // like to see the error rather than have the thread transparently continue without stopping.
    cb(Err("Destination to run until matched no location."));
  } else {
    // Success, can continue the thread.
    cb(Err());
  }
}

}  // namespace zxdb
