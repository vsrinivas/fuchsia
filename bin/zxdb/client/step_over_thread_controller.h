// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/thread_controller.h"

namespace zxdb {

class FinishThreadController;
class StepInRangeThreadController;

// This controller causes the thread to single-step as long as the CPU is in
// a given address range or any stack frame called from it. Contrast with
// the StepInRangeThreadController which does not do the sub-frames.
//
// This class works by:
//   1. Single-stepping in the range.
//   2. When the range is exited, see if the address is in a sub-frame.
//   3. Step out of the sub-frame if so, exit if not.
//   4. Repeat.
class StepOverThreadController : public ThreadController {
 public:
  enum ConstructionMode {
    // Does "next" for a specific address range.
    kAddressRange,

    // Does "next" for the current source line. If there is no source line,
    // this does "kInstruction" mode.
    kSourceLine,

    // Does "next" for the current CPU instruction (stepping over calls).
    kInstruction
  };

  // Constructor for kSourceLine and kInstruction modes. It will initialize
  // itself to the thread's current position when the thread is attached.
  explicit StepOverThreadController(ConstructionMode mode);

  // Constructor for a kAddressRange mode. Continues execution as long as the
  // IP is in [begin, end)
  StepOverThreadController(uint64_t begin, uint64_t end);

  ~StepOverThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

 private:
  ConstructionMode construction_mode_;

  // The fingerprint of the frame we're stepping in. Anything newer than this
  // is a child frame we should step through, and anything older than this
  // means we exited the function and should stop stepping.
  FrameFingerprint frame_fingerprint_;

  // Always non-null.
  std::unique_ptr<StepInRangeThreadController> step_in_range_;

  // Only set when we're stepping out.
  std::unique_ptr<FinishThreadController> finish_;
};

}  // namespace zxdb
