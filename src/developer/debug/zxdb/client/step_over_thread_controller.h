// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_OVER_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_OVER_THREAD_CONTROLLER_H_

#include <memory>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/function_return_info.h"
#include "src/developer/debug/zxdb/client/step_mode.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/developer/debug/zxdb/common/address_ranges.h"
#include "src/developer/debug/zxdb/symbols/file_line.h"

namespace zxdb {

class AddressRanges;
class FinishThreadController;
class Frame;
class StepThreadController;

// This controller causes the thread to single-step as long as the CPU is in a given address range
// or any stack frame called from it. Contrast with the StepThreadController which does not do the
// sub-frames.
//
// This class works by:
//   1. Single-stepping in the range.
//   2. When the range is exited, see if the address is in a sub-frame.
//   3. Step out of the sub-frame if so, exit if not.
//   4. Repeat.
class StepOverThreadController : public ThreadController {
 public:
  // Constructor for kSourceLine and kInstruction modes. It will initialize itself to the thread's
  // current position when the thread is attached.
  //
  // The function_return callback (if supplied) will be issued when the "step over" terminates with
  // the completion of the function. It will not be called for every function that is skipped over
  // as part of execution.
  explicit StepOverThreadController(StepMode mode, FunctionReturnCallback function_return = {},
                                    fit::deferred_callback on_done = {});

  // Constructor for a kAddressRange mode (the mode is implicit). Continues execution as long as the
  // IP is in range.
  explicit StepOverThreadController(AddressRanges range,
                                    FunctionReturnCallback function_return = {},
                                    fit::deferred_callback on_done = {});

  ~StepOverThreadController() override;

  // Sets a callback that the caller can use to control whether excecution stops in a given
  // subframe. The subframe will be one called directly from the code range being stopped over.
  //
  // This allows implementation of operations like "step until you get to a function". When the
  // callback returns true, the "step over" operation will complete at the current location (this
  // will then destroy the controller and indirectly the callback object).
  //
  // When empty (the default), all subframes will be continued.
  void set_subframe_should_stop_callback(fit::function<bool(const Frame*)> cb) {
    subframe_should_stop_callback_ = std::move(cb);
  }

  // ThreadController implementation.
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Step Over"; }

 private:
  StepMode step_mode_;

  // When non-null indicates callback to check for stopping in subframes. See the setter above.
  fit::function<bool(const Frame*)> subframe_should_stop_callback_;

  // When construction_mode_ == kSourceLine, this represents the line information of the line we're
  // stepping over.
  //
  // IMPORTANT: This class should not perform logic or comparisons on this value. Reasoning about
  // the file/line in the current stack frame should be delegated to the StepThreadController.
  FileLine file_line_;

  // When construction_mode_ == kAddressRange, this represents the address range we're stepping
  // over.
  AddressRanges address_ranges_;

  // The fingerprint of the frame we're stepping in. Anything newer than this is a child frame we
  // should step through, and anything older than this means we exited the function and should stop
  // stepping.
  FrameFingerprint frame_fingerprint_;

  // Always non-null, manages stepping in the original function.
  std::unique_ptr<StepThreadController> step_into_;

  // Only set when we're stepping out to get back to the original function.
  std::unique_ptr<FinishThreadController> finish_;

  FunctionReturnInfo return_info_;
  FunctionReturnCallback function_return_callback_;  // Possibly null.
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_OVER_THREAD_CONTROLLER_H_
