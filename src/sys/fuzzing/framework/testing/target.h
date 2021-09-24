// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TESTING_TARGET_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TESTING_TARGET_H_

#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <stdint.h>
#include <zircon/syscalls/exception.h>

#include <thread>

#include "src/lib/fxl/macros.h"

namespace fuzzing {

// This class encapsulates a fake target process. It simply launches and then waits to crash or
// exit.
class TestTarget final {
 public:
  TestTarget() = default;
  ~TestTarget();

  // Spawns the process, and returns a copy of the spawned process handle.
  zx::process Launch();

  // Asks the spawned process to crash.
  void Crash();

  // Asks the spawned process to exit with the given |exitcode|.
  void Exit(int32_t exitcode);

  // Waits for the spawned process to completely terminate.
  void Join();

 private:
  void Reset();

  zx::process process_;
  zx::channel local_;
  zx::channel exception_channel_;
  std::thread exception_thread_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(TestTarget);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TESTING_TARGET_H_
