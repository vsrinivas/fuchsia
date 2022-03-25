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

using ::test::fuzzer::RelayPtr;
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

TestFuzzer::TestFuzzer() : executor_(MakeExecutor(loop_.dispatcher())), eventpair_(executor_) {}

int TestFuzzer::Initialize(int *argc, char ***argv) {
  __sanitizer_cov_8bit_counters_init(module_.counters(), module_.counters_end());
  __sanitizer_cov_pcs_init(module_.pcs(), module_.pcs_end());
  return ZX_OK;
}

int TestFuzzer::TestOneInput(const uint8_t *data, size_t size) {
  zx_status_t retval = ZX_ERR_SHOULD_WAIT;
  auto task =
      fpromise::make_promise([this, relay = RelayPtr(), connect = Future<SignaledBuffer>()](
                                 Context &context) mutable -> ZxResult<> {
        // First, connect to the unit test via the relay, if necessary.
        if (eventpair_.IsConnected() && !relay) {
          return fpromise::ok();
        }
        if (!relay) {
          auto context = sys::ComponentContext::Create();
          auto status = context->svc()->Connect(relay.NewRequest(executor_->dispatcher()));
          if (status != ZX_OK) {
            FX_LOGS(ERROR) << "Failed to connect to relay: " << zx_status_get_string(status);
            return fpromise::error(status);
          }
        }
        if (!connect) {
          Bridge<SignaledBuffer> bridge;
          relay->WatchTestData(bridge.completer.bind());
          connect = bridge.consumer.promise_or(fpromise::error());
        }
        if (!connect(context)) {
          return fpromise::pending();
        }
        if (connect.is_error()) {
          return fpromise::error(ZX_ERR_PEER_CLOSED);
        }
        auto signaled_buffer = connect.take_value();
        test_input_buffer_.LinkReserved(std::move(signaled_buffer.test_input));
        feedback_buffer_.LinkMirrored(std::move(signaled_buffer.feedback));
        eventpair_.Pair(std::move(signaled_buffer.eventpair));
        relay->Finish();
        return fpromise::ok();
      })
          .and_then([this, data, size] {
            test_input_buffer_.Clear();
            test_input_buffer_.Write(data, size);
            // Notify the unit test that the test input is ready, and wait for its notification that
            // feedback is ready.
            return AsZxResult(eventpair_.SignalPeer(0, kStart));
          })
          .and_then(eventpair_.WaitFor(kStart))
          .and_then([this](const zx_signals_t &observed) {
            return AsZxResult(eventpair_.SignalSelf(observed, 0));
          })
          .and_then([this]() -> ZxResult<> {
            const auto *feedback =
                reinterpret_cast<const RelayedFeedback *>(feedback_buffer_.data());
            for (size_t i = 0; i < feedback->num_counters; ++i) {
              const auto *counter = &feedback->counters[i];
              module_[counter->offset] = counter->value;
            }
            if (feedback->leak_suspected) {
              FX_CHECK(malloc_hook_) << "__sanitizer_install_malloc_and_free_hooks was not called.";
              // The lack of a corresponding call to |free_hook| should make libFuzzer suspect a
              // leak.
              malloc_hook_(this, sizeof(*this));
            }
            has_leak_ = false;
            switch (feedback->result) {
              case FuzzResult::NO_ERRORS:
                // Notify the unit test that the fuzzer completed the run.
                eventpair_.SignalPeer(0, kFinish);
                return fpromise::ok();
              case FuzzResult::BAD_MALLOC:
                printf("DEDUP_TOKEN: BAD_MALLOC\n");
                BadMalloc();
                break;
              case FuzzResult::CRASH:
                printf("DEDUP_TOKEN: CRASH\n");
                Crash();
                break;
              case FuzzResult::DEATH:
                printf("DEDUP_TOKEN: DEATH\n");
                Death();
                break;
              case FuzzResult::EXIT:
                printf("DEDUP_TOKEN: EXIT\n");
                exit(0);
                break;
              case FuzzResult::LEAK:
                has_leak_ = true;
                return fpromise::ok();
              case FuzzResult::OOM:
                printf("DEDUP_TOKEN: OOM\n");
                OOM();
                break;
              case FuzzResult::TIMEOUT:
                printf("DEDUP_TOKEN: TIMEOUT\n");
                Timeout();
                break;
            }
            FX_NOTREACHED();
            return fpromise::error(ZX_ERR_INTERNAL);
          })
          .then([&retval](const ZxResult<> &result) {
            retval = result.is_ok() ? ZX_OK : result.error();
            return fpromise::ok();
          });
  // Compare with async-test.h. Unlike a real fuzzer, this fake fuzzer runs its async loop on the
  // current thread. To make |LLVMFuzzerTestOneInput| synchronous, this method needs to periodically
  // kick the loop until the promise above completes.
  executor_->schedule_task(std::move(task));
  loop_.RunUntilIdle();
  while (retval == ZX_ERR_SHOULD_WAIT) {
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
    loop_.RunUntilIdle();
  }
  return retval;
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
  zx::nanosleep(zx::time::infinite());
}

}  // namespace fuzzing
