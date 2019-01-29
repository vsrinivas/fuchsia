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

FinishThreadController::FinishThreadController(const Stack& stack,
                                               size_t frame_to_finish) {
  FXL_DCHECK(frame_to_finish < stack.size());

  // TODO(brettw) use new async frame fingerprint computation here.
  if (frame_to_finish == stack.size() - 1) {
    if (stack.has_all_frames()) {
      // Request to finish the oldest stack frame.
      to_address_ = 0;
      from_frame_fingerprint_ = *stack.GetFrameFingerprint(frame_to_finish);
    } else {
      // Need to sync frames so we can find the calling one. Save the IP and
      // SP which will be used to re-find the frame in question.
      // TODO(brettw) this won't handle inline frame selection properly.
      frame_ip_ = stack[frame_to_finish]->GetAddress();
      frame_ip_ = stack[frame_to_finish]->GetStackPointer();
    }
  } else {
    // Common case where the frame to finish has a previous frame and the
    // frame fingerprint and return address are known.
    FXL_DCHECK(stack.size() > frame_to_finish + 1);
    // TODO(brettw) this won't handle inline frame selection properly.
    to_address_ = stack[frame_to_finish + 1]->GetAddress();
    from_frame_fingerprint_ = *stack.GetFrameFingerprint(frame_to_finish);
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

  if (from_frame_fingerprint_.is_valid()) {
    // The fingerprint was already computed in the constructor, can skip
    // directly to setting up the breakpoint.
    InitWithFingerprint(std::move(cb));
  } else {
    // Need to request stack frames to get the frame fingerpring and return
    // address (both of which require previous frames). We can capture |this|
    // here since the thread owns this thread controller.
    thread->GetStack().SyncFrames([ this, cb = std::move(cb) ](const Err& err) {
      InitWithStack(err, this->thread()->GetStack(), std::move(cb));
    });
  }
}

ThreadController::ContinueOp FinishThreadController::GetContinueOp() {
  if (until_controller_)
    return until_controller_->GetContinueOp();
  return ContinueOp::Continue();
}

void FinishThreadController::InitWithStack(const Err& err, const Stack& stack,
                                           std::function<void(const Err&)> cb) {
  // Note if this was called asynchronously the thread could be resumed
  // and it could have no frames, or totally different ones.
  if (err.has_error()) {
    cb(err);
    return;
  }

  // Find the frame corresponding to the requested one.
  constexpr size_t kNotFound = std::numeric_limits<size_t>::max();
  size_t requested_index = kNotFound;
  for (size_t i = 0; i < stack.size(); i++) {
    if (stack[i]->GetAddress() == frame_ip_ &&
        stack[i]->GetStackPointer() == frame_sp_) {
      requested_index = i;
      break;
    }
  }
  if (requested_index == kNotFound) {
    cb(Err("The stack changed before \"finish\" could start."));
    return;
  }

  if (requested_index == stack.size() - 1) {
    // "Finish" from the bottom-most stack frame just continues the
    // program to completion.
    cb(Err());
    return;
  }

  // The stack frame to exit to is just the next one up.
  to_address_ = stack[requested_index + 1]->GetAddress();
  if (!to_address_) {
    // Often the bottom-most stack frame will have a 0 IP which obviously
    // we can't return to. Treat this the same as when returning from the
    // last frame and just continue.
    cb(Err());
    return;
  }
  from_frame_fingerprint_ =
      *thread()->GetStack().GetFrameFingerprint(requested_index);
  InitWithFingerprint(std::move(cb));
}

void FinishThreadController::InitWithFingerprint(
    std::function<void(const Err&)> cb) {
  if (to_address_ == 0) {
    // There is no return address. This will happen when trying to finish the
    // oldest stack frame. Nothing to do.
    cb(Err());
    return;
  }

  until_controller_ = std::make_unique<UntilThreadController>(
      InputLocation(to_address_), from_frame_fingerprint_,
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
  cb(Err());
}

}  // namespace zxdb
