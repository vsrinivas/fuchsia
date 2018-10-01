// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <functional>

#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "garnet/bin/zxdb/symbols/input_location.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class Err;
class System;
class Target;

class UntilThreadController : public ThreadController {
 public:
  // Runs a thread until the given location. The location will only be matched
  // if the stack base pointer position of the location is greater than end_sp
  // this means that the stack has grown up to a higher frame. When end_bp is
  // 0, every stack pointer will be larger and it will always trigger.
  // Supporting the stack pointer allows this class to be used for stack-aware
  // options (as a subset of "finish" for example).
  explicit UntilThreadController(InputLocation location);

  // Runs to the given location until the current frame is either equal to
  // |newest_frame|, or older than it. This allows stepping backward in the
  // call stack.
  UntilThreadController(InputLocation location, FrameFingerprint newest_frame);

  ~UntilThreadController() override;

  // ThreadController implementation:
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Until"; }

 private:
  System* GetSystem();
  Target* GetTarget();

  // Callback for when the breakpoint is set. The parameter is the continue
  // callback from thread initialization.
  void OnBreakpointSet(const Err& err, std::function<void(const Err&)> cb);

  InputLocation location_;

  // Indicates the frame. Any frame equal to this or older means stop, newer
  // frames than this keep running.
  //
  // When no frame checking is needed, this will be !is_valid().
  FrameFingerprint newest_threadhold_frame_;

  std::function<void(const Err&)> error_callback_;

  fxl::WeakPtr<Breakpoint> breakpoint_;

  fxl::WeakPtrFactory<UntilThreadController> weak_factory_;
};

}  // namespace zxdb
