// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_THREAD_CONTROLLER_H_

#include <optional>

#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/function_return_info.h"
#include "src/developer/debug/zxdb/client/step_mode.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/developer/debug/zxdb/common/address_ranges.h"
#include "src/developer/debug/zxdb/symbols/file_line.h"

namespace zxdb {

class FinishThreadController;

// Implements a low-level "step into" command. It knows how to step by source lines, over a range of
// addresses, or by single instruction.
//
// This is the main low-level thread controller used by other ones. Generally programmatic uses
// (e.g. from with "step over") will use this class. It will not generally be used directly, a
// user-level "step into" should use the StepIntoThreadController which provides some additional
// functionality.
//
// When stepping by file/line, this class will generate synthetic exceptions and adjust the stack to
// simulate stepping into inline function calls (even though there is no actual call instruction).
class StepThreadController : public ThreadController {
 public:
  // Constructor for kSourceLine and kInstruction modes. It will initialize itself to the thread's
  // current position when the thread is attached.
  //
  // The function_return callback (if supplied) will be issued when the "step into" terminates with
  // the completion of the function.
  explicit StepThreadController(StepMode mode, FunctionReturnCallback function_return = {},
                                fit::deferred_callback on_done = {});

  // Steps given the source file/line.
  explicit StepThreadController(const FileLine& line, FunctionReturnCallback function_return = {},
                                fit::deferred_callback on_done = {});

  // Constructor for a kAddressRange mode (the mode is implicit). Continues execution as long as the
  // IP is in range.
  explicit StepThreadController(AddressRanges ranges, FunctionReturnCallback function_return = {},
                                fit::deferred_callback on_done = {});

  ~StepThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Step"; }

 private:
  enum class StepIntoInline {
    // Actually performs the inline step, modifying the hidden ambiguous Stack items as necessary.
    kCommit,

    // Does the operations to compute whether an inline step can be completed and returns the
    // corresponding result, but does not actually change any state.
    kQuery
  };

  // Attempts to step into an inline function that starts and the current stack addresss. This will
  // make it look like the user stepped into the inline function even though no code was executed.
  //
  // If there is an inline to step into, this will fix up the current stack to appear as if the
  // inline function is stepped into and return true. False means there was not an inline function
  // starting at the current address.
  bool TrySteppingIntoInline(StepIntoInline command);

  StepMode step_mode_;
  FrameFingerprint original_frame_fingerprint_;

  // When step_mode_ == kSourceLine, this represents the line information and the stack fingerprint
  // of where stepping started. The file/line may be given in the constructor or we may need to
  // compute it upon init from the current location (whether it needs setting is encoded by the
  // optional).
  std::optional<FileLine> file_line_;

  // Range of addresses we're currently stepping in. This may change when we're stepping over source
  // lines and wind up in a region with no line numbers. It will be empty when stepping by
  // instruction.
  AddressRanges current_ranges_;

  // Handles stepping out or through special functions we want to ignore.
  std::unique_ptr<ThreadController> function_step_;

  FunctionReturnInfo return_info_;
  FunctionReturnCallback function_return_callback_;  // Possibly null.
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_THREAD_CONTROLLER_H_
