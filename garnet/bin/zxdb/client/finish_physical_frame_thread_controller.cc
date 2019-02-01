// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/finish_physical_frame_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/until_thread_controller.h"
#include "garnet/bin/zxdb/common/err.h"
#include "lib/fxl/logging.h"

namespace zxdb {

FinishPhysicalFrameThreadController::FinishPhysicalFrameThreadController(
    Stack& stack, size_t frame_to_finish)
    : frame_to_finish_(frame_to_finish), weak_factory_(this) {
  FXL_DCHECK(frame_to_finish < stack.size());
  FXL_DCHECK(!stack[frame_to_finish]->IsInline());

#ifndef NDEBUG
  // Stash for validation later.
  frame_ip_ = stack[frame_to_finish]->GetAddress();
#endif
}

FinishPhysicalFrameThreadController::~FinishPhysicalFrameThreadController() =
    default;

FinishPhysicalFrameThreadController::StopOp
FinishPhysicalFrameThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (until_controller_)
    return until_controller_->OnThreadStop(stop_type, hit_breakpoints);

  // When there's no "until" controller, this controller just said "continue"
  // to step out of the oldest stack frame. Therefore, any stops at this level
  // aren't ours.
  return kContinue;
}

void FinishPhysicalFrameThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  Stack& stack = thread->GetStack();

#ifndef NDEBUG
  // The stack must not have changed from construction to this call. There are
  // no async requests that need to happen during this time, just registration
  // with the thread. Otherwise the frame fingerprint computation needs to be
  // scheduled in the constructor which complicates the async states of this
  // function (though it's possible in the future if necessary).
  FXL_DCHECK(stack.size() > frame_to_finish_);
  FXL_DCHECK(stack[frame_to_finish_]->GetAddress() == frame_ip_);
#endif

  if (auto found_fingerprint = stack.GetFrameFingerprint(frame_to_finish_)) {
    // Common case where the frame to finish has a previous frame and the
    // frame fingerprint and return address are known. If the frame's
    // fingerprint can be computed, that means that the previous stack frame is
    // available (or known not to exist).
    // TODO(brettw) this won't handle inline frame selection properly.
    InitWithFingerprint(*found_fingerprint);
    cb(Err());
  } else {
    // Fingerprint needs an asynchronous request.
    stack.GetFrameFingerprint(
        frame_to_finish_,
        [weak_this = weak_factory_.GetWeakPtr(), cb = std::move(cb)](
            const Err& err, size_t new_index, FrameFingerprint fingerprint) {
          // Callback could come after this stepping is torn down, so don't
          // even issue the callback in that case.
          if (!weak_this)
            return;

          if (err.has_error()) {
            cb(err);
          } else {
            // Save possibly-updated frame index before dispatching.
            weak_this->frame_to_finish_ = new_index;
            weak_this->InitWithFingerprint(fingerprint);
            cb(Err());
          }
        });
  }
}

ThreadController::ContinueOp
FinishPhysicalFrameThreadController::GetContinueOp() {
  // Once this thread starts running, the frame index is invalid.
  frame_to_finish_ = static_cast<size_t>(-1);

  if (until_controller_)
    return until_controller_->GetContinueOp();

  // This will happen when there's no previous frame so there's no address
  // to return to. Unconditionally continue.
  return ContinueOp::Continue();
}

void FinishPhysicalFrameThreadController::InitWithFingerprint(
    FrameFingerprint fingerprint) {
  if (frame_to_finish_ >= thread()->GetStack().size() - 1) {
    // Finishing the last frame. There is no return address so there's no
    // setup necessary to step, just continue.
    return;
  }

  // The address we're returning to is that of the previous frame,
  uint64_t to_addr = thread()->GetStack()[frame_to_finish_ + 1]->GetAddress();
  if (!to_addr)
    return;  // Previous stack frame is null, just continue.

  until_controller_ = std::make_unique<UntilThreadController>(
      InputLocation(to_addr), fingerprint,
      UntilThreadController::kRunUntilOlderFrame);

  // Give the "until" controller a dummy callback and execute the callback
  // ASAP. The until controller executes the callback once it knows that the
  // breakpoint set has been complete (round-trip to the target system).
  //
  // Since we provide an address there's no weirdness with symbols and we don't
  // have to worry about matching 0 locations. If the breakpoint set fails, the
  // caller address is invalid and stepping is impossible so it doesn't matter.
  // We can run faster without waiting for the round-trip, and the IPC will
  // serialize so the breakpoint set happens before the thread resume.
  until_controller_->InitWithThread(thread(), [](const Err&) {});
}

}  // namespace zxdb
