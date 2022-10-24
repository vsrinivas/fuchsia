// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_INTO_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_INTO_THREAD_CONTROLLER_H_

#include <optional>

#include "src/developer/debug/zxdb/client/function_return_info.h"
#include "src/developer/debug/zxdb/client/step_mode.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/developer/debug/zxdb/common/address_ranges.h"
#include "src/developer/debug/zxdb/symbols/file_line.h"

namespace zxdb {

class StepOverThreadController;
class StepThreadController;

// Implements a user-level "step into" command. On top of the regular step into, this provides
// an option to skip function prologues.
//
// Function prologues are the code at the beginning of a function that sets up the stack frame,
// and function parameters and backtraces might not be correct in this address range. Therefore,
// we usually want to step through this prologue when stepping into a new function so that the
// state is valid when the user inspects it.
class StepIntoThreadController : public ThreadController {
 public:
  // Constructor for kSourceLine and kInstruction modes. It will initialize itself to the thread's
  // current position when the thread is attached.
  //
  // The function_return callback (if supplied) will be issued when the "step into" terminates with
  // the completion of the function.
  explicit StepIntoThreadController(StepMode mode, FunctionReturnCallback function_return = {},
                                    fit::deferred_callback on_done = {});

  // Steps given the source file/line.
  explicit StepIntoThreadController(const FileLine& line,
                                    FunctionReturnCallback function_return = {},
                                    fit::deferred_callback on_done = {});

  // Constructor for a kAddressRange mode (the mode is implicit). Continues execution as long as the
  // IP is in range.
  explicit StepIntoThreadController(AddressRanges ranges,
                                    FunctionReturnCallback function_return = {},
                                    fit::deferred_callback on_done = {});

  ~StepIntoThreadController() override;

  // Controls whether this class skips function prologues that it might step into. See class
  // comment above. Defaults to true.
  bool should_skip_prologue() const { return should_skip_prologue_; }
  void set_should_skip_prologue(bool skip) { should_skip_prologue_ = skip; }

  // ThreadController implementation.
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Step Into"; }

 private:
  bool should_skip_prologue_ = true;

  FrameFingerprint original_frame_fingerprint_;

  std::unique_ptr<StepThreadController> step_into_;
  std::unique_ptr<StepOverThreadController> skip_prologue_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_INTO_THREAD_CONTROLLER_H_
