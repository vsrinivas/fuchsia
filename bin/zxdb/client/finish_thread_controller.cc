// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/finish_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/until_thread_controller.h"
#include "garnet/bin/zxdb/common/err.h"

namespace zxdb {

FinishThreadController::FinishThreadController(const Frame* frame_to_finish)
    : ThreadController(),
      frame_ip_(frame_to_finish->GetAddress()),
      frame_bp_(frame_to_finish->GetBasePointer()) {}

FinishThreadController::~FinishThreadController() = default;

FinishThreadController::StopOp FinishThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  return until_controller_->OnThreadStop(stop_type, hit_breakpoints);
}

void FinishThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  if (thread->HasAllFrames()) {
    InitWithFrames(std::move(cb));
  } else {
    // Need to asynchronously request the thread's frames. We can capture
    // |this| here since the thread owns this class.
    thread->SyncFrames(
        [ this, cb = std::move(cb) ]() { InitWithFrames(std::move(cb)); });
  }
}

void FinishThreadController::InitWithFrames(
    std::function<void(const Err&)> cb) {
  // Note the frames may have changed from when the constructor was called or
  // they could even be empty (if the thread was already running or racily
  // resumed before the backtrace completed).
  auto frames = thread()->GetFrames();

  // Find the frame corresponding to the reqested one.
  constexpr size_t kNotFound = std::numeric_limits<size_t>::max();
  size_t requested_index = kNotFound;
  for (size_t i = 0; i < frames.size(); i++) {
    if (frames[i]->GetAddress() == frame_ip_ &&
        frames[i]->GetBasePointer() == frame_bp_) {
      requested_index = i;
      break;
    }
  }
  if (requested_index == kNotFound) {
    cb(Err("The stack changed before \"finish\" could start."));
    return;
  }

  if (requested_index == frames.size() - 1) {
    // "Finish" from the bottom-most stack frame just continues the
    // program to completion.
    cb(Err());
    return;
  }

  // The stack frame to exit to is just the next one up. The "until" controller
  // will manage things from here.
  Frame* step_to = frames[requested_index + 1];

  // UntilThreadController will break when the BP is greater than the argument.
  // Do "BasePointer - 1" to match the exact BP and anything greater.
  //
  // An alternative implementation would be to look at the current frame's BP
  // (which avoids having to sync the full list of frames) and anything greater
  // must mean we're out of that frame. But that doesn't handle the case where
  // the current location is in the function prologue, in which case the BP
  // of the current frame could be the same as the previuos one.
  uint64_t threadhold_bp = step_to->GetBasePointer() - 1;
  until_controller_ = std::make_unique<UntilThreadController>(
      InputLocation(step_to->GetAddress()), threadhold_bp);
  until_controller_->InitWithThread(thread(), std::move(cb));
}

}  // namespace zxdb
