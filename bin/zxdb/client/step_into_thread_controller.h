// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/thread_controller.h"

namespace zxdb {

class StepInRangeThreadController;

// Implements "step into". This single-steps a thread until the instruction
// pointer is on a different source line than given. If there are no symbols,
// this controller falls back on single-stepping instructions.
class StepIntoThreadController : public ThreadController {
 public:
  StepIntoThreadController();
  ~StepIntoThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

 private:
  // This will be null if there is no range that could be computed and the
  // thread should have its instructions single-stepped instead.
  std::unique_ptr<StepInRangeThreadController> step_in_range_;
};

}  // namespace zxdb
