// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_UNTIL_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_UNTIL_THREAD_CONTROLLER_H_

#include <stdint.h>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class Err;
class System;
class Target;

class UntilThreadController : public ThreadController {
 public:
  enum FrameComparison {
    // The program will run until the current frame is older than the given one. In this case if the
    // frame fingerprints compare equal, the program will continue to run. Anything older will stop.
    kRunUntilOlderFrame,

    // Stops when the current frame is the same as or older than the given one.
    kRunUntilEqualOrOlderFrame
  };

  // Runs a thread until the given location. The location will only be matched if the stack base
  // pointer position of the location is greater than end_sp this means that the stack has grown up
  // to a higher frame. When end_bp is 0, every stack pointer will be larger and it will always
  // trigger. Supporting the stack pointer allows this class to be used for stack-aware options (as
  // a subset of "finish" for example).
  explicit UntilThreadController(std::vector<InputLocation> locations);

  // Runs to the given location until the current frame compares to the given frame according to the
  // given comparator. This allows stepping backward in the call stack.
  UntilThreadController(std::vector<InputLocation> locations, FrameFingerprint newest_frame,
                        FrameComparison cmp);

  ~UntilThreadController() override;

  // ThreadController implementation:
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Until"; }

 private:
  System* GetSystem();
  Target* GetTarget();

  std::vector<InputLocation> locations_;

  // Indicates the frame. This frame is compared to the current one according to the comparison_
  // function.
  //
  // When no frame checking is needed, the threshold frame will be !is_valid().
  FrameFingerprint threshold_frame_;
  FrameComparison comparison_ = kRunUntilOlderFrame;

  fxl::WeakPtr<Breakpoint> breakpoint_;

  fxl::WeakPtrFactory<UntilThreadController> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_UNTIL_THREAD_CONTROLLER_H_
