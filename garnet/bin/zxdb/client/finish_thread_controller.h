// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <vector>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class FinishPhysicalFrameThreadController;
class Stack;
class StepOverThreadController;

// Thread controller that runs a given stack frame to its completion. This
// can finish more than one frame at once, and there could be any combination
// of physical and inline frames being exited from.
//
// This works by first finishing to the nearest physical frame using the
// FinishPhysicalFrameThreadController (if there is no physical frame above the
// one being finished, this will be a no-op). Then any inline frames will be
// iteratively finished using the StepOverThreadController to step over the
// inline code ranges until the desired frame is reached.
class FinishThreadController : public ThreadController {
 public:
  // Finishes the given frame of the stack, leaving control at frame
  // |frame_to_finish + 1] when the controller is complete.
  //
  // The frame_to_finish must have its fingerprint computable. This means that
  // either you're finishing frame 0, or have synced all frames.
  FinishThreadController(Stack& stack, size_t frame_to_finish);

  ~FinishThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Finish"; }

 private:
  // Creates the controller for stepping out of the inline function at the top
  // of the stack. Issues the callback in all cases. Returns false on failure.
  bool CreateInlineStepOverController(std::function<void(const Err&)> cb);

  // Index of the frame to finish. Invalid after the thread is resumed.
  size_t frame_to_finish_;

#ifndef NDEBUG
  // IP of the frame to step out of. This is a sanity check to make sure the
  // stack didn't change between construction and InitWithThread.
  uint64_t frame_ip_;
#endif

  // Will be non-null when stepping out of the nearest physical frame. When
  // doing the subsequent inline step this will be null.
  std::unique_ptr<FinishPhysicalFrameThreadController>
      finish_physical_controller_;

  // The frame being stepped out of. This will be set when the frame being
  // stepped out of is an inline frame. Otherwise, only the physical frame
  // stepper is required.
  FrameFingerprint from_inline_frame_fingerprint_;

  // Will be non-null when stepping out of inline frames. When doing the
  // initial step out of a physical frame, this will be null.
  std::unique_ptr<StepOverThreadController> step_over_controller_;

  fxl::WeakPtrFactory<FinishThreadController> weak_factory_;
};

}  // namespace zxdb
