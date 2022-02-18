// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/libfuzzer/testing/fuzzer.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include <memory>
#include <random>

#include <sanitizer/common_interface_defs.h>
#include <sanitizer/lsan_interface.h>
#include <test/fuzzer/cpp/fidl.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/result.h"
#include "src/sys/fuzzing/common/sancov.h"

namespace fuzzing {

using ::test::fuzzer::RelaySyncPtr;
using ::test::fuzzer::SignaledBuffer;

std::unique_ptr<TestFuzzer> gFuzzer;

}  // namespace fuzzing

// libFuzzer expects the sanitizer to provide several weak symbols. For testing, this code can fake
// the sanitizer's behavior by implementing those symbols itself.
extern "C" {

// Create and initialize the fuzzer object.
int LLVMFuzzerInitialize(int *argc, char ***argv) {
  fuzzing::gFuzzer = std::make_unique<fuzzing::TestFuzzer>();
  return fuzzing::gFuzzer->Initialize(argc, argv);
}

// Forward certain calls to the |gFuzzer| object, including the required fuzz target function.
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return fuzzing::gFuzzer->TestOneInput(data, size);
}

int __lsan_do_recoverable_leak_check() { return fuzzing::gFuzzer->DoRecoverableLeakCheck(); }

int __sanitizer_acquire_crash_state() { return fuzzing::gFuzzer->AcquireCrashState(); }

// Save the various hook functions provided by libFuzzer.
int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void *, size_t),
                                              void (*free_hook)(const volatile void *)) {
  fuzzing::gFuzzer->set_malloc_hook(malloc_hook);
  return 1;
}

void __sanitizer_set_death_callback(void (*death_callback)(void)) {
  fuzzing::gFuzzer->set_death_callback(death_callback);
}

// The remaining external functions expected by libFuzzer can just be stubbed out.
void __lsan_enable() {}
void __lsan_disable() {}
void __lsan_do_leak_check() {}
void __sanitizer_log_write(const char *buf, size_t len) {}
void __sanitizer_purge_allocator() {}
void __sanitizer_print_memory_profile(size_t, size_t) {}
void __sanitizer_print_stack_trace() {}
void __sanitizer_symbolize_pc(void *, const char *fmt, char *out_buf, size_t out_buf_size) {}
int __sanitizer_get_module_and_offset_for_pc(void *pc, char *module_path, size_t module_path_len,
                                             void **pc_offset) {
  return 0;
}
void __sanitizer_set_report_fd(void *) {}

}  // extern "C"

namespace fuzzing {

// Public methods.

int TestFuzzer::Initialize(int *argc, char ***argv) {
  __sanitizer_cov_8bit_counters_init(module_.counters(), module_.counters_end());
  __sanitizer_cov_pcs_init(module_.pcs(), module_.pcs_end());
  return ZX_OK;
}

int TestFuzzer::TestOneInput(const uint8_t *data, size_t size) {
  if (!coordinator_.is_valid()) {
    RelaySyncPtr relay;
    auto context = sys::ComponentContext::Create();
    auto status = context->svc()->Connect(relay.NewRequest());
    FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
    SignaledBuffer data;
    status = relay->WatchTestData(&data);
    FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
    test_input_buffer_.LinkReserved(std::move(data.test_input));
    feedback_buffer_.LinkMirrored(std::move(data.feedback));
    coordinator_.Pair(std::move(data.eventpair));
  }
  test_input_buffer_.Clear();
  test_input_buffer_.Write(data, size);
  if (!coordinator_.SignalPeer(kStart)) {
    return ZX_ERR_PEER_CLOSED;
  }
  auto observed = coordinator_.AwaitSignal();
  if ((observed & ZX_EVENTPAIR_PEER_CLOSED) != 0) {
    return ZX_ERR_PEER_CLOSED;
  }
  const auto *feedback = reinterpret_cast<const RelayedFeedback *>(feedback_buffer_.data());
  for (size_t i = 0; i < feedback->num_counters; ++i) {
    const auto *counter = &feedback->counters[i];
    module_[counter->offset] = counter->value;
  }
  if (feedback->leak_suspected) {
    FX_CHECK(malloc_hook_) << "__sanitizer_install_malloc_and_free_hooks was not called.";
    // The lack of a corresponding call to |free_hook| should make libFuzzer suspect a leak.
    malloc_hook_(this, sizeof(*this));
  }
  has_leak_ = false;
  switch (feedback->result) {
    case FuzzResult::NO_ERRORS:
      coordinator_.SignalPeer(kFinish);
      break;
    case FuzzResult::BAD_MALLOC:
      printf("DEDUP_TOKEN: BAD_MALLOC\n");
      BadMalloc();
      return -1;
    case FuzzResult::CRASH:
      printf("DEDUP_TOKEN: CRASH\n");
      Crash();
      return -1;
    case FuzzResult::DEATH:
      printf("DEDUP_TOKEN: DEATH\n");
      Death();
      return -1;
    case FuzzResult::EXIT:
      printf("DEDUP_TOKEN: EXIT\n");
      exit(0);
      return -1;
    case FuzzResult::LEAK:
      has_leak_ = true;
      break;
    case FuzzResult::OOM:
      printf("DEDUP_TOKEN: OOM\n");
      OOM();
      return -1;
    case FuzzResult::TIMEOUT:
      printf("DEDUP_TOKEN: TIMEOUT\n");
      Timeout();
      return -1;
    default:
      FX_NOTREACHED();
      break;
  }
  return ZX_OK;
}

int TestFuzzer::DoRecoverableLeakCheck() {
  if (has_leak_) {
    printf("DEDUP_TOKEN: LEAK\n");
    return 1;
  }
  return 0;
}

int TestFuzzer::AcquireCrashState() { return !crash_state_acquired_.exchange(true); }

// Private methods.

void TestFuzzer::BadMalloc() {
  FX_CHECK(malloc_hook_) << "__sanitizer_install_malloc_and_free_hooks was not called.";
  malloc_hook_(this, size_t(-1));
}

void TestFuzzer::Crash() { __builtin_trap(); }

void TestFuzzer::Death() {
  FX_CHECK(death_callback_) << "__sanitizer_set_death_callback was not called.";
  death_callback_();
  exit(1);
}

void TestFuzzer::OOM() {
  // Grow at a rate of ~100 Mb/s. Even with a low RSS limit, it may take a couple seconds to OOM,
  // as libFuzzer's RSS thread runs once per second.
  std::minstd_rand prng;
  std::vector<std::vector<uint8_t>> blocks;
  const size_t block_size = 1ULL << 20;
  while (true) {
    std::vector<uint8_t> block(block_size, static_cast<uint8_t>(prng()));
    blocks.push_back(std::move(block));
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
  }
}

void TestFuzzer::Timeout() {
  // Make sure libFuzzer's -timeout flag is set to something reasonable before calling this!
  Waiter waiter = [](zx::time deadline) { return zx::nanosleep(deadline); };
  WaitFor("ever", &waiter);
}

}  // namespace fuzzing
