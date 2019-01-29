// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/thread_controller.h"

namespace zxdb {

class Frame;
class Stack;
class UntilThreadController;

// Thread controller that runs a given stack frame to its completion. This
// can finish more than one frame at once, and there could be a combination of
// physical and inline frames being exited from.
class FinishThreadController : public ThreadController {
 public:
  // Finishes the given frame of the stack, leaving control at frame
  // |frame_to_finish + 1] when the controller is complete.
  FinishThreadController(const Stack& stack, size_t frame_to_finish);

  ~FinishThreadController() override;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Finish"; }

 private:
  // Callback for when the thread has loaded its stack frames. This will
  // compute the to_frame_fingerprint_.
  void InitWithStack(const Err& err, const Stack& stack, std::function<void(const Err&)> cb);

  bool HaveAddressAndFingerprint() const;

  // Does final initialization given the to_frame_fingerprint_ is known or we
  // know we don't need it.
  void InitWithFingerprint(std::function<void(const Err&)> cb);

  // The instruction and stack pointer of the frame when the address and
  // fingerprint are not known. The SP allows disambiguation for two frames
  // at the same address. These will be nonzero only when we need to
  // asynchronously request data to compute the address or fingerprint.
  uint64_t frame_ip_ = 0;
  uint64_t frame_sp_ = 0;

  // The from_frame_fingerprint_ will indicate the frame we're trying to
  // finish. This being valid indicates that the stack has been fetched and
  // the frame in question located.
  //
  // The to_address_ will be the address we're returning to. This will be
  // computed at the same time as from_frame_fingerprint_, but may be 0 if
  // the user is trying to finish the oldest stack frame (there's no address
  // to return to).
  uint64_t to_address_ = 0;
  FrameFingerprint from_frame_fingerprint_;

  // Will be non-null when stepping out. During initialization or when stepping
  // out of the earliest stack frame, this can be null.
  std::unique_ptr<UntilThreadController> until_controller_;
};

}  // namespace zxdb
