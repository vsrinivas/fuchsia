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

UntilThreadController::UntilThreadController(Thread* in_thread,
                                             InputLocation location,
                                             uint64_t end_sp)
    : ThreadController(in_thread), end_sp_(end_sp) {
  BreakpointSettings settings;
  settings.scope = BreakpointSettings::Scope::kThread;
  settings.scope_target = GetTarget();
  settings.scope_thread = thread();
  settings.location = std::move(location);

  // Frame-tied triggers can't be one-shot because we need to check the stack
  // every time it triggers. In the non-frame case the one-shot breakpoint will
  // be slightly more efficient.
  settings.one_shot = end_sp_ == 0;

  breakpoint_ = GetSystem()->CreateNewInternalBreakpoint()->GetWeakPtr();
  // OK to capture |this| since our destructor will destroy the breakpoint.
  breakpoint_->SetSettings(settings,
                           [this](const Err& err) { OnSetComplete(err); });
}

UntilThreadController::~UntilThreadController() {
  if (breakpoint_)
    GetSystem()->DeleteBreakpoint(breakpoint_.get());
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

  if (!end_sp_)
    return kStop;  // No stack check necessary, always stop.

  auto frames = thread()->GetFrames();
  if (frames.empty()) {
    FXL_NOTREACHED();  // Should always have a current frame on stop.
    return kStop;
  }

  // The stack grows downward. Want to stop the thread only when the frame is
  // before (greater than) the input one, which means anything <= should
  // continue.
  if (frames[0]->GetStackPointer() <= end_sp_)
    return kContinue;
  return kStop;
}

System* UntilThreadController::GetSystem() {
  return &thread()->session()->system();
}

Target* UntilThreadController::GetTarget() {
  return thread()->GetProcess()->GetTarget();
}

void UntilThreadController::OnSetComplete(const Err& err) {
  if (err.has_error()) {
    if (error_callback_)
      error_callback_(err);

    // This controller can no longer work so just remove ourselves.
    NotifyControllerDone();
  }
}

}  // namespace zxdb
