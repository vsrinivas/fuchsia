// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/thread_controller.h"

namespace zxdb {

// This controller causes the thread to single-step as long as the CPU is in
// a given address range. It is used as a component of some of the higher-level
// step controllers such as "step into". Contrast with the
// StepOverRangeThreadController which also steps over calls.
class StepInRangeThreadController : public ThreadController {
 public:
  // Continues execution as long as the IP is in [begin, end).
  StepInRangeThreadController(uint64_t begin, uint64_t end);

  ~StepInRangeThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

 private:
  uint64_t begin_;
  uint64_t end_;
};

}  // namespace zxdb
