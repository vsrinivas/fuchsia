// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_EVENT_QUEUE_H_
#define SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_EVENT_QUEUE_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <stdint.h>

#include <atomic>
#include <deque>
#include <mutex>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/sync-wait.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageEvent;
using ::fuchsia::fuzzer::CoverageEventPtr;
using ::fuchsia::fuzzer::InstrumentedProcess;
using ::fuchsia::fuzzer::LlvmModule;
using ::fuchsia::fuzzer::Payload;

class CoverageEventQueue final {
 public:
  CoverageEventQueue() = default;
  ~CoverageEventQueue();

  // These are used by |InstrumentationImpl| methods.
  Options GetOptions() FXL_LOCKS_EXCLUDED(mutex_);
  void AddProcess(uint64_t target_id, InstrumentedProcess instrumented) FXL_LOCKS_EXCLUDED(mutex_);
  void AddLlvmModule(uint64_t target_id, LlvmModule llvm_module) FXL_LOCKS_EXCLUDED(mutex_);

  // These are used by |CoverageProvider| methods.
  void SetOptions(Options options) FXL_LOCKS_EXCLUDED(mutex_);
  CoverageEventPtr GetEvent() FXL_LOCKS_EXCLUDED(mutex_);

  void Stop();

 private:
  void AddEvent(uint64_t target_id, Payload payload) FXL_LOCKS_EXCLUDED(mutex_);

  std::mutex mutex_;
  Options options_ FXL_GUARDED_BY(mutex_);
  std::deque<CoverageEventPtr> events_ FXL_GUARDED_BY(mutex_);
  bool stopped_ FXL_GUARDED_BY(mutex_) = false;
  SyncWait available_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CoverageEventQueue);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_EVENT_QUEUE_H_
