// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FINISH_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FINISH_THREAD_CONTROLLER_H_

#include <optional>
#include <vector>

#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/function_return_info.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class FinishPhysicalFrameThreadController;
class Stack;
class StepOverThreadController;
class StepThreadController;

// Thread controller that runs a given stack frame to its completion. This can finish more than one
// frame at once, and there could be any combination of physical and inline frames being exited
// from.
//
// This works by first finishing to the nearest physical frame using the
// FinishPhysicalFrameThreadController (if there is no physical frame above the one being finished,
// this will be a no-op). Then any inline frames will be iteratively finished using the
// StepOverThreadController to step over the inline code ranges until the desired frame is reached.
class FinishThreadController : public ThreadController {
 public:
  // Finishes the given frame of the stack, leaving control at frame |frame_to_finish + 1] when the
  // controller is complete.
  //
  // The frame_to_finish must have its fingerprint computable. This means that either you're
  // finishing frame 0, or have synced all frames.
  //
  // The optional callback will be issued when a physical frame is stepped out of. It will be on
  // the instruction immediately following the return. This controller might be used to step out
  // of inline frames or a physical frame followed by some inline frames. This will be issued on the
  // outermost physical frame, and never on any inline frames. So it might not get called at all,
  // and the call might not be the outermost function call from the user's perspective.
  FinishThreadController(Stack& stack, size_t frame_to_finish,
                         FunctionReturnCallback cb = FunctionReturnCallback(),
                         fit::deferred_callback on_done = {});

  ~FinishThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Finish"; }

 private:
  // Handles stepping out of the frames. Having this separate allows the "frame 0" handling at the
  // end to be pulled out in one place.
  StopOp OnThreadStopFrameStepping(debug_ipc::ExceptionType stop_type,
                                   const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints);

  // Creates the controller for stepping out of the inline function at the top of the stack. Issues
  // the callback in all cases. Returns false on failure.
  bool CreateInlineStepOverController(fit::callback<void(const Err&)> cb);

  // Index of the frame to finish. Invalid after the thread is resumed.
  size_t frame_to_finish_;

#ifndef NDEBUG
  // IP of the frame to step out of. This is a sanity check to make sure the stack didn't change
  // between construction and InitWithThread.
  uint64_t frame_ip_;
#endif

  // Will be non-null when stepping out of the nearest physical frame. When doing the subsequent
  // inline step this will be null.
  std::unique_ptr<FinishPhysicalFrameThreadController> finish_physical_controller_;

  // The frame being stepped out of. This will be set when the frame being stepped out of is an
  // inline frame. Otherwise, only the physical frame stepper is required.
  FrameFingerprint from_inline_frame_fingerprint_;

  // Will be non-null when stepping out of inline frames. When doing the initial step out of a
  // physical frame, this will be null.
  std::unique_ptr<StepOverThreadController> step_over_inline_controller_;

  // This controller manages the skipping of "line 0" after the finish operations.
  std::unique_ptr<StepThreadController> step_over_line_0_controller_;

  FunctionReturnCallback function_return_callback_;  // Possibly null.

  fxl::WeakPtrFactory<FinishThreadController> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FINISH_THREAD_CONTROLLER_H_
