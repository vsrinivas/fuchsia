// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <functional>

#include "garnet/bin/zxdb/client/input_location.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
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
  UntilThreadController(InputLocation location, uint64_t end_bp = 0);

  ~UntilThreadController() override;

  // ThreadController implementation:
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override;
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

 private:
  System* GetSystem();
  Target* GetTarget();

  // Callback for when the breakpoint is set. The parameter is the continue
  // callback from thread initialization.
  void OnBreakpointSet(const Err& err, std::function<void(const Err&)> cb);

  InputLocation location_;
  uint64_t end_bp_ = 0;

  std::function<void(const Err&)> error_callback_;

  fxl::WeakPtr<Breakpoint> breakpoint_;

  fxl::WeakPtrFactory<UntilThreadController> weak_factory_;
};

}  // namespace zxdb
