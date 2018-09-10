// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/step_mode.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "garnet/bin/zxdb/common/address_range.h"

namespace zxdb {

class FinishThreadController;
class StepThreadController;

// This controller causes the thread to single-step as long as the CPU is in
// a given address range or any stack frame called from it. Contrast with
// the StepThreadController which does not do the sub-frames.
//
// This class works by:
//   1. Single-stepping in the range.
//   2. When the range is exited, see if the address is in a sub-frame.
//   3. Step out of the sub-frame if so, exit if not.
//   4. Repeat.
class StepOverThreadController : public ThreadController {
 public:
  // Constructor for kSourceLine and kInstruction modes. It will initialize
  // itself to the thread's current position when the thread is attached.
  explicit StepOverThreadController(StepMode mode);

  // Constructor for a kAddressRange mode (the mode is implicit). Continues
  // execution as long as the IP is in range.
  explicit StepOverThreadController(AddressRange range);

  ~StepOverThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

 private:
  // The fingerprint of the frame we're stepping in. Anything newer than this
  // is a child frame we should step through, and anything older than this
  // means we exited the function and should stop stepping.
  FrameFingerprint frame_fingerprint_;

  // Always non-null, manages stepping in the original function.
  std::unique_ptr<StepThreadController> step_into_;

  // Only set when we're stepping out to get back to the original function.
  std::unique_ptr<FinishThreadController> finish_;
};

}  // namespace zxdb
