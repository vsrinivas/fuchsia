// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_
#define SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/sync/completion.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/processargs.h>

#include <atomic>
#include <memory>

#include <test/fuzzer/cpp/fidl.h>

#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/testing/module.h"
#include "src/sys/fuzzing/common/testing/signal-coordinator.h"
#include "src/sys/fuzzing/libfuzzer/testing/feedback.h"

namespace fuzzing {

class TestFuzzer {
 public:
  TestFuzzer() = default;
  ~TestFuzzer() = default;

  using MallocHook = void (*)(const volatile void*, size_t);
  void set_malloc_hook(MallocHook malloc_hook) { malloc_hook_ = malloc_hook; }

  using DeathCallback = void (*)();
  void set_death_callback(DeathCallback death_callback) { death_callback_ = death_callback; }

  // Implementation of |LLVMFuzzerInitialize|.
  int Initialize(int* argc, char*** argv);

  // Implementation of |LLVMFuzzerTestOneInput|.
  int TestOneInput(const uint8_t* data, size_t size);

  // Implementation of |__lsan_do_recoverable_leak_check|.
  int DoRecoverableLeakCheck();

  // Implementation of |__sanitizer_acquire_crash_state|.
  int AcquireCrashState();

 private:
  // Triggers various error conditions.
  void BadMalloc();
  void Crash();
  void Death();
  void OOM();
  void Timeout();

  FakeModule module_;
  MallocHook malloc_hook_ = nullptr;
  DeathCallback death_callback_ = nullptr;
  bool has_leak_ = false;
  std::atomic<bool> crash_state_acquired_ = false;

  FakeSignalCoordinator coordinator_;
  SharedMemory test_input_buffer_;
  SharedMemory feedback_buffer_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_
