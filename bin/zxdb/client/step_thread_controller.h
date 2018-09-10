// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/step_mode.h"
#include "garnet/bin/zxdb/client/symbols/file_line.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "garnet/bin/zxdb/common/address_range.h"

namespace zxdb {

// Implements "step into". This single-steps a thread until the instruction
// pointer is in a different region (line/range/instruction as defined by the
// StepMode).
class StepThreadController : public ThreadController {
 public:
  // Constructor for kSourceLine and kInstruction modes. It will initialize
  // itself to the thread's current position when the thread is attached.
  explicit StepThreadController(StepMode mode);

  // Constructor for a kAddressRange mode (the mode is implicit). Continues
  // execution as long as the IP is in range.
  explicit StepThreadController(AddressRange range);

  ~StepThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

  // When used as a nested controller, the thread may be stopped by another
  // controller's action and control given to this controller. In this case,
  // we want to evaluate the step condition regardless of the stop_type.
  StopOp OnThreadStopIgnoreType(
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints);

 private:
  StepMode step_mode_;

  // When construction_mode_ == kSourceLine, this represents the line
  // information.
  FileLine file_line_;

  // Range of addresses we're currently stepping in. This may change when we're
  // stepping over source lines and wind up in a region with no line numbers.
  // It will be empty when stepping by instruction.
  AddressRange current_range_;
};

}  // namespace zxdb
