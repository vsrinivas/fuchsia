// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <functional>

#include "garnet/bin/zxdb/client/thread_controller.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class Err;
struct InputLocation;
class System;
class Target;

class UntilThreadController : public ThreadController {
 public:
  // Runs the thread until the given location. The location will only be
  // matched if the stack position of the location is greater than end_sp
  // this means that the stack has grown up to a higher frame. When end_sp
  // is 0, every stack pointer will be larger and it will always trigger.
  // Supporting the stack pointer allows this class to be used for stack-
  // aware options (as a subset of "finish" for example).
  UntilThreadController(Thread* thread, InputLocation location,
                        uint64_t end_sp = 0);

  ~UntilThreadController() override;

  // The set up for the operation can fail. Normally this will trigger the
  // controller to just remove itself from the thread. If a client needs to
  // know about failures, it can set a callback here that will be executed
  // right before the controller removes itself.
  void set_error_callback(std::function<void(const Err&)> cb) {
    error_callback_ = std::move(cb);
  }

  // ThreadController implementation:
  StopOp OnThreadStop(debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;

 private:
  System* GetSystem();
  Target* GetTarget();

  void OnSetComplete(const Err& err);

  uint64_t end_sp_ = 0;

  std::function<void(const Err&)> error_callback_;

  fxl::WeakPtr<Breakpoint> breakpoint_;
};

}  // namespace zxdb
