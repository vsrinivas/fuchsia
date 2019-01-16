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

class FinishThreadController : public ThreadController {
 public:
  // Tag classes that make the constructor variants more clear at the call
  // sites (it's hard to tell whether the argument is being stepped "from" or
  // "to").
  class FromFrame {};
  class ToFrame {};

  // Steps out of / "from" the given frame, leaving execution at the next
  // instruction in the calling (older) frame.
  FinishThreadController(FromFrame, const Frame* frame);

  // Steps "to" to the given frame address/fingerprint. Any newer frame
  // fingerprints will be ignored (execution will continue). The thread will
  // only stop at the address when the current frame matches (or is older than)
  // the to_frame_fingerprint.
  FinishThreadController(ToFrame, uint64_t to_address,
                         const FrameFingerprint& to_frame_fingerprint);

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
  void InitWithStack(const Stack& stack, std::function<void(const Err&)> cb);

  bool HaveAddressAndFingerprint() const;

  // Does final initialization given the to_frame_fingerprint_ is known or we
  // know we don't need it.
  void InitWithFingerprint(std::function<void(const Err&)> cb);

  // The instruction and stack pointer of the frame when the address and
  // fingerprint are not known. The SP allows disambiguation for two frames
  // at the same address.
  uint64_t frame_ip_ = 0;
  uint64_t frame_sp_ = 0;

  // When set, to_address_ will be nonzero and to_frame_fingerprint_ will be
  // is_valid(). See HaveAddressAndFingerprint().
  uint64_t to_address_ = 0;
  FrameFingerprint to_frame_fingerprint_;

  // Will be non-null when stepping out. During initialization or when stepping
  // out of the earliest stack frame, this can be null.
  std::unique_ptr<UntilThreadController> until_controller_;
};

}  // namespace zxdb
