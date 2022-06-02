// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/libfuzzer/testing/fuzzer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include <random>

#include <test/fuzzer/cpp/fidl.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/result.h"
#include "src/sys/fuzzing/common/testing/sanitizer.h"
#include "src/sys/fuzzing/libfuzzer/testing/feedback.h"

namespace fuzzing {

using ::test::fuzzer::Relay;
using ::test::fuzzer::RelayPtr;
using ::test::fuzzer::SignaledBuffer;

TestFuzzer::TestFuzzer() {
  context_ = ComponentContext::Create();
  eventpair_ = std::make_unique<AsyncEventPair>(context_->executor());
}

int TestFuzzer::TestOneInput(const uint8_t *data, size_t size) {
  zx_status_t retval = ZX_ERR_SHOULD_WAIT;
  auto task = fpromise::make_promise([this, relay = RelayPtr(), connect = Future<SignaledBuffer>()](
                                         Context &context) mutable -> ZxResult<> {
                // First, connect to the unit test via the relay, if necessary.
                if (eventpair_->IsConnected() && !relay) {
                  return fpromise::ok();
                }
                if (!relay) {
                  auto handler = context_->MakeRequestHandler<Relay>();
                  auto executor = context_->executor();
                  handler(relay.NewRequest(executor->dispatcher()));
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
                eventpair_->Pair(std::move(signaled_buffer.eventpair));
                relay->Finish();
                return fpromise::ok();
              })
                  .and_then([this, data, size] {
                    test_input_buffer_.Clear();
                    test_input_buffer_.Write(data, size);
                    // Notify the unit test that the test input is ready, and wait for its
                    // notification that feedback is ready.
                    return AsZxResult(eventpair_->SignalPeer(0, kStart));
                  })
                  .and_then(eventpair_->WaitFor(kStart))
                  .and_then([this](const zx_signals_t &observed) {
                    return AsZxResult(eventpair_->SignalSelf(observed, 0));
                  })
                  .and_then([this]() -> ZxResult<> {
                    const auto *feedback =
                        reinterpret_cast<const RelayedFeedback *>(feedback_buffer_.data());
                    for (size_t i = 0; i < feedback->num_counters; ++i) {
                      const auto *counter = &feedback->counters[i];
                      SetCoverage(counter->offset, counter->value);
                    }
                    if (feedback->leak_suspected) {
                      // Without a call to the |free_hook|, the fake sanitizer should suspect a
                      // leak.
                      Malloc(sizeof(*this));
                    }
                    switch (feedback->result) {
                      case FuzzResult::NO_ERRORS:
                        // Notify the unit test that the fuzzer completed the run.
                        return AsZxResult(eventpair_->SignalPeer(0, kFinish));
                      case FuzzResult::BAD_MALLOC:
                        printf("DEDUP_TOKEN: BAD_MALLOC\n");
                        Malloc(size_t(-1));
                        break;
                      case FuzzResult::CRASH:
                        printf("DEDUP_TOKEN: CRASH\n");
                        Crash();
                        break;
                      case FuzzResult::DEATH:
                        printf("DEDUP_TOKEN: DEATH\n");
                        Die();
                        break;
                      case FuzzResult::EXIT:
                        // Don't call exit() here; the atexit handlers will invoke the executors
                        // destructors, which will panic since we're mid-task. Signal via error.
                        printf("DEDUP_TOKEN: EXIT\n");
                        return fpromise::error(ZX_ERR_STOP);
                      case FuzzResult::LEAK:
                        LeakMemory();
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
  context_->ScheduleTask(std::move(task));
  while (true) {
    if (auto status = context_->RunUntilIdle(); status != ZX_OK) {
      FX_LOGS(WARNING) << "Loop stopped unexpectedly: " << zx_status_get_string(status);
      return status;
    }
    // See the comment on the |FuzzResult::EXIT| case above. It is safe to call exit() here.
    if (retval == ZX_ERR_STOP) {
      exit(0);
    }
    if (retval != ZX_ERR_SHOULD_WAIT) {
      return retval;
    }
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
  }
}

void TestFuzzer::Crash() { __builtin_trap(); }

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

// Forward the fuzz target function required by libFuzzer to the |gFuzzer| object.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static fuzzing::TestFuzzer fuzzer;
  return fuzzer.TestOneInput(data, size);
}
