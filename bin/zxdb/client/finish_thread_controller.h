// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <vector>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "lib/fxl/memory/weak_ptr.h"

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
  FinishThreadController(Stack& stack, size_t frame_to_finish);

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
  // Called when both the frame fingerprint and the thread are known. Does
  // final initialization.
  void InitWithFingerprintAndThread();

  // The from_frame_fingerprint_ will indicate the frame we're trying to
  // finish. This being valid indicates that the stack has been fetched and
  // the frame in question located (the operation may be async).
  //
  // This uses std::optional so the fingerprint can be tested to see if it's
  // been set, even if the stack returns the null fingerprint (might happen
  // for the bottom of the stack).
  //
  // The to_address_ will be the address we're returning to. This will be
  // computed at the same time as from_frame_fingerprint_, but may be 0 if
  // the user is trying to finish the oldest stack frame (there's no address
  // to return to).
  uint64_t to_address_ = 0;
  std::optional<FrameFingerprint> from_frame_fingerprint_;

  // The fingerprint can be computed asynchronously with initialization. This
  // holds the InitWithThread callback for InitWithFingerprintAndThread().
  std::function<void(const Err&)> init_callback_;

  // Will be non-null when stepping out. During initialization or when stepping
  // out of the earliest stack frame, this can be null.
  std::unique_ptr<UntilThreadController> until_controller_;

  fxl::WeakPtrFactory<FinishThreadController> weak_factory_;
};

}  // namespace zxdb
