// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_TESTING_TARGET_H_
#define SRC_SYS_FUZZING_REALMFUZZER_TESTING_TARGET_H_

#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <stdint.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/child-process.h"
#include "src/sys/fuzzing/realmfuzzer/target/process.h"

namespace fuzzing {

// This class encapsulates a fake target process. It simply launches and then waits to crash or
// exit.
class TestTarget final {
 public:
  explicit TestTarget(ExecutorPtr executor);
  ~TestTarget();

  uint64_t id() const { return id_; }

  // Spawns the process, and returns a copy of the spawned process handle.
  zx::process Launch();

  // Returns a promise that asks the spawned process to crash and completes when it terminates.
  ZxPromise<> Crash();

  // Returns a promise that asks the spawned process to exit with the given |exitcode| and completes
  // when it terminates.
  ZxPromise<> Exit(int32_t exitcode);

 private:
  // Waits for the spawned process to completely terminate.
  ZxPromise<> AwaitTermination();

  void Reset();

  ExecutorPtr executor_;
  ChildProcess target_;
  zx::channel local_;
  uint64_t id_ = kInvalidTargetId;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(TestTarget);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_TESTING_TARGET_H_
