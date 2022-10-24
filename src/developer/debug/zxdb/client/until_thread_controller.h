// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_UNTIL_THREAD_CONTROLLER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_UNTIL_THREAD_CONTROLLER_H_

#include <stdint.h>

#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class BreakpointLocation;
class Err;
class System;
class Target;

// The "until" thread controller continues until a given instruction is reached. It sets a
// breakpoint at the desired location(s) and continues execution.
//
// Setting the breakpoint may fail in several different ways. In the simplest case the location
// to run to isn't found (symbol resolution failure). The breakpoint could also fail to be set.
// In addition to weird errors and race conditions that could cause the breakpoint set to fail, this
// can happen if the breakpoint location is in unwritable memory, like the vDSO (this can happen
// during certain stepping operations involving syscalls).
//
// These errors are indicated by the callback given to InitWithThread() which can be issued
// asynchronously. Callers should be sure to handle these errors as otherwise program execution will
// continue and the user's stepping location can be lost!
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
  explicit UntilThreadController(std::vector<InputLocation> locations,
                                 fit::deferred_callback on_done = {});

  // Runs to the given location until the current frame compares to the given frame according to the
  // given comparator. This allows stepping backward in the call stack.
  UntilThreadController(std::vector<InputLocation> locations, FrameFingerprint newest_frame,
                        FrameComparison cmp, fit::deferred_callback on_done = {});

  ~UntilThreadController() override;

  // ThreadController implementation:
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override;
  ContinueOp GetContinueOp() override;
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  const char* GetName() const override { return "Until"; }

  // Returns the resolved locations where this thread controller is running to. When active, this
  // will always contain at least one element (InitWithThread() will report error if there are no
  // addresses resolved).
  std::vector<const BreakpointLocation*> GetLocations() const;

 private:
  System* GetSystem();
  Target* GetTarget();

  void OnBreakpointSetComplete(const Err& err, fit::callback<void(const Err&)> cb);

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
