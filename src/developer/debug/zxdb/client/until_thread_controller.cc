// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/until_thread_controller.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_location.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"

namespace zxdb {

UntilThreadController::UntilThreadController(std::vector<InputLocation> locations,
                                             fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)), locations_(std::move(locations)), weak_factory_(this) {}

UntilThreadController::UntilThreadController(std::vector<InputLocation> locations,
                                             FrameFingerprint newest_frame, FrameComparison cmp,
                                             fit::deferred_callback on_done)
    : ThreadController(std::move(on_done)),
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
  breakpoint_->SetSettings(settings, [weak_controller = weak_factory_.GetWeakPtr(),
                                      cb = std::move(cb)](const Err& err) mutable {
    if (weak_controller)
      weak_controller->OnBreakpointSetComplete(err, std::move(cb));
  });
}

void UntilThreadController::OnBreakpointSetComplete(const Err& err,
                                                    fit::callback<void(const Err&)> cb) {
  if (err.has_error())
    return cb(err);  // Error updating breakpoint.

  // Validate that the breakpoint matched some locations that look reasonable. Note that this
  // information is available synchronously after Breakpoint::SetSettings() since it's just doing
  // symbol matching, but we defer checking to here to simplify error checking and issuing the
  // callback from one place.
  const std::vector<const BreakpointLocation*>& locs = GetLocations();
  if (locs.empty()) {
    // Setting the breakpoint may have resolved to no locations and the breakpoint is now pending.
    // For "until" this is not good because if the user does "until SomethingNonexistant" they would
    // like to see the error rather than have the thread transparently continue without stopping.
    cb(Err("Destination to run until matched no location."));
  } else {
    if (enable_debug_logging()) {
      // Log the addresses we resolved.
      std::ostringstream log;
      log << "Matched addr(s): ";

      for (size_t i = 0; i < locs.size(); i++) {
        log << to_hex_string(locs[i]->GetLocation().address());
        if (i + 1 < locs.size())
          log << ", ";
      }
      std::string log_str = log.str();
      Log(log_str.c_str());
    }
    cb(Err());
  }
}

ThreadController::ContinueOp UntilThreadController::GetContinueOp() {
  // Stopping the thread is done via a breakpoint, so the thread can always be resumed with no
  // qualifications.
  return ContinueOp::Continue();
}

ThreadController::StopOp UntilThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (stop_type == debug_ipc::ExceptionType::kNone) {
    // A "none" exception type will be passed in to us to see if we apply to the current location
    // when being initialized in nested controller context.
    //
    // Since the "until" controller only triggers on breakpoints, we always want to continue in
    // these cases. Even if the breakpoint is at the current address, continuing at this address
    // will hit it again.
    return kContinue;
  }

  // Other controllers such as the StepOverRangeThreadController can use this as a sub-controller.
  // If the controllers don't care about breakpoint set failures, they may start using the thread
  // right away without waiting for the callback in InitWithThread() to asynchronously complete
  // (indicating the breakpoint was set successful).
  //
  // This is generally fine, we just need to be careful not to do anything in OnBreakpointSet() that
  // the code in this function depends on.
  if (!breakpoint_) {
    // Our internal breakpoint shouldn't be deleted out from under ourselves.
    FX_NOTREACHED();
    return kUnexpected;
  }

  // Only care about stops if one of the breakpoints hit was ours. Don't check the stop_type since
  // as long as the breakpoint was hit, we don't care how the program got there (it could have
  // single-stepped to the breakpoint).
  Breakpoint* our_breakpoint = breakpoint_.get();
  bool is_our_breakpoint = false;
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
    Log("No frame check required, we're done.");
    return kStopDone;
  }

  const Stack& stack = thread()->GetStack();
  if (stack.empty()) {
    FX_NOTREACHED();  // Should always have a current frame on stop.
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
  Log("Found target frame (or older), 'until' operation complete.");
  return kStopDone;
}

std::vector<const BreakpointLocation*> UntilThreadController::GetLocations() const {
  if (!breakpoint_) {
    FX_NOTREACHED();
    return {};
  }

  return const_cast<const Breakpoint*>(breakpoint_.get())->GetLocations();
}

System* UntilThreadController::GetSystem() { return &thread()->session()->system(); }

Target* UntilThreadController::GetTarget() { return thread()->GetProcess()->GetTarget(); }

}  // namespace zxdb
