// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/until_thread_controller.h"

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/input_location.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "lib/fxl/logging.h"

namespace zxdb {

UntilThreadController::UntilThreadController(InputLocation location,
                                             uint64_t end_bp)
    : ThreadController(),
      location_(std::move(location)),
      end_bp_(end_bp),
      weak_factory_(this) {}

UntilThreadController::~UntilThreadController() {
  if (breakpoint_)
    GetSystem()->DeleteBreakpoint(breakpoint_.get());
}

void UntilThreadController::InitWithThread(Thread* thread,
                                           std::function<void(const Err&)> cb) {
  set_thread(thread);

  BreakpointSettings settings;
  settings.scope = BreakpointSettings::Scope::kThread;
  settings.scope_target = GetTarget();
  settings.scope_thread = thread;
  settings.location = std::move(location_);

  // Frame-tied triggers can't be one-shot because we need to check the stack
  // every time it triggers. In the non-frame case the one-shot breakpoint will
  // be slightly more efficient.
  settings.one_shot = end_bp_ == 0;

  breakpoint_ = GetSystem()->CreateNewInternalBreakpoint()->GetWeakPtr();
  // The breakpoint may post the callback asynchronously, so we can't be sure
  // this class is still alive when this callback is issued, even though we
  // destroy the breakpoint in the destructor.
  breakpoint_->SetSettings(
      settings, [ weak_this = weak_factory_.GetWeakPtr(), cb ](const Err& err) {
        if (weak_this)
          weak_this->OnBreakpointSet(err, std::move(cb));
      });
}

ThreadController::ContinueOp UntilThreadController::GetContinueOp() {
  // Stopping the thread is done via a breakpoint, so the thread can always be
  // resumed with no qualifications.
  return ContinueOp::Continue();
}

ThreadController::StopOp UntilThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (!breakpoint_) {
    // Our internal breakpoint shouldn't be deleted out from under ourselves.
    FXL_NOTREACHED();
    return kContinue;
  }

  // Only care about stops if one of the breakpoints hit was ours.
  Breakpoint* our_breakpoint = breakpoint_.get();
  bool is_our_breakpoint = true;
  for (auto& hit : hit_breakpoints) {
    if (hit && hit.get() == our_breakpoint) {
      is_our_breakpoint = true;
      break;
    }
  }
  if (!is_our_breakpoint)
    return kContinue;  // Not our breakpoint.

  if (!end_bp_)
    return kStop;  // No stack check necessary, always stop.

  auto frames = thread()->GetFrames();
  if (frames.empty()) {
    FXL_NOTREACHED();  // Should always have a current frame on stop.
    return kStop;
  }

  // The stack grows downward. Want to stop the thread only when the frame is
  // before (greater than) the input one, which means anything <= should
  // continue.
  if (frames[0]->GetBasePointer() <= end_bp_)
    return kContinue;
  return kStop;
}

System* UntilThreadController::GetSystem() {
  return &thread()->session()->system();
}

Target* UntilThreadController::GetTarget() {
  return thread()->GetProcess()->GetTarget();
}

void UntilThreadController::OnBreakpointSet(
    const Err& err, std::function<void(const Err&)> cb) {
  if (err.has_error()) {
    // Breakpoint setting failed.
    cb(err);
  } else if (!breakpoint_ || breakpoint_->GetLocations().empty()) {
    // Setting the breakpoint may have resolved to no locations and the
    // breakpoint is now pending. For "until" this is not good because if the
    // user does "until SometyhingNonexistant" they would like to see the error
    // rather than have the thread transparently continue without stopping.
    cb(Err("Destination to run until matched no location."));
  } else {
    // Success, can continue the thread.
    cb(Err());
  }
}

}  // namespace zxdb
