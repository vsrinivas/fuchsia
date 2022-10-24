// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_INTO_SPECIFIC_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_INTO_SPECIFIC_THREAD_CONTROLLER_H_

#include <memory>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/step_mode.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/file_line.h"

namespace zxdb {

class AddressRanges;
class FinishThreadController;
class Frame;
class StepIntoThreadController;
class StepOverThreadController;
class StepThreadController;

// Combines "step over" for a given range, followed by a "step into". This is used where there's
// a specific function that the caller wants to step into.
//
// If execution leaves the "step over" range by jumping anywhere other than to the instruction
// immediately following the range, execution still stop without stepping into. This is in case
// the desired "into" destination is conditionally skipped.
class StepIntoSpecificThreadController : public ThreadController {
 public:
  explicit StepIntoSpecificThreadController(AddressRange over_range,
                                            fit::deferred_callback on_done = {});

  ~StepIntoSpecificThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Step Into Specific"; }

 private:
  // The address where we expect to step into.
  TargetPointer into_address_;

  // On one of these will be non-null, depending on what phase we're in.
  std::unique_ptr<StepOverThreadController> step_over_;
  std::unique_ptr<StepIntoThreadController> step_into_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_INTO_SPECIFIC_THREAD_CONTROLLER_H_
