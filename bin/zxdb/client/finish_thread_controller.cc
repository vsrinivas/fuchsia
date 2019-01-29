// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/finish_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/until_thread_controller.h"
#include "garnet/bin/zxdb/common/err.h"
#include "lib/fxl/logging.h"

namespace zxdb {

FinishThreadController::FinishThreadController(Stack& stack,
                                               size_t frame_to_finish)
    : weak_factory_(this) {
  FXL_DCHECK(frame_to_finish < stack.size());

  if (auto found_fingerprint = stack.GetFrameFingerprint(frame_to_finish)) {
    // Common case where the frame to finish has a previous frame and the
    // frame fingerprint and return address are known. If the frame's
    // fingerprint can be computed, that means that the previous stack frame is
    // available (or known not to exist).
    // TODO(brettw) this won't handle inline frame selection properly.
    from_frame_fingerprint_ = *found_fingerprint;
    if (frame_to_finish == stack.size() - 1)
      to_address_ = 0;  // Request to finish oldest stack frame.
    else
      to_address_ = stack[frame_to_finish + 1]->GetAddress();
  } else {
    // Fingerprint needs an asynchronous request.
    stack.GetFrameFingerprint(
        frame_to_finish,
        [weak_this = weak_factory_.GetWeakPtr()](
            const Err& err, size_t new_index, FrameFingerprint fingerprint) {
          if (!weak_this)
            return;

          weak_this->from_frame_fingerprint_ = fingerprint;
          if (weak_this->thread())
            weak_this->InitWithFingerprintAndThread();
          // Otherwise the callback will be issued when the thread is set.
        });
  }
}

FinishThreadController::~FinishThreadController() = default;

FinishThreadController::StopOp FinishThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (until_controller_)
    return until_controller_->OnThreadStop(stop_type, hit_breakpoints);

  // When there's no "until" controller, this controller just said "continue"
  // to step out of the oldest stack frame. Therefore, any stops at this level
  // aren't ours.
  return kContinue;
}

void FinishThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  init_callback_ = std::move(cb);

  if (from_frame_fingerprint_)
    InitWithFingerprintAndThread();
  // Otherwise the callback will be called when the fingerprint is know.
}

ThreadController::ContinueOp FinishThreadController::GetContinueOp() {
  if (until_controller_)
    return until_controller_->GetContinueOp();
  return ContinueOp::Continue();
}

void FinishThreadController::InitWithFingerprintAndThread() {
  FXL_DCHECK(init_callback_ && from_frame_fingerprint_);

  if (to_address_ == 0) {
    // There is no return address. This will happen when trying to finish the
    // oldest stack frame. Nothing to do.
    init_callback_(Err());
    init_callback_ = nullptr;
    return;
  }

  until_controller_ = std::make_unique<UntilThreadController>(
      InputLocation(to_address_), *from_frame_fingerprint_,
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

  init_callback_(Err());
  init_callback_ = nullptr;
}

}  // namespace zxdb
