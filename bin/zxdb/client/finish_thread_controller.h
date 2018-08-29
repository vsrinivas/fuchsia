// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/thread_controller.h"

namespace zxdb {

class Frame;
class UntilThreadController;

class FinishThreadController : public ThreadController {
 public:
  // The frame_to_finish will be finished and execution will be left in the
  // frame above it.
  explicit FinishThreadController(const Frame* frame_to_finish);
  ~FinishThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

 private:
  // Callback for when the thread has loaded its frames.
  void InitWithFrames(std::function<void(const Err&)> cb);

  // The instruction and base pointer of the frame to finish (leaving execution
  // at the frame before this).
  uint64_t frame_ip_ = 0;
  uint64_t frame_bp_ = 0;

  std::unique_ptr<UntilThreadController> until_controller_;
};

}  // namespace zxdb
