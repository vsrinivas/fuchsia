// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "signal-coordinator.h"

#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <atomic>

#include <gtest/gtest.h>

namespace fuzzing {
namespace {

// Test fixtures

// This class provides a SignalHandler that can be used as the |on_signal| parameter for
// |SignalCoordinator::Create| and |SignalCoordinator::Pair|. It allows tests to block on signal
// receipt. Tests can also control when the |SignalCoordinator::WaitLoop| exits by calling
// |set_result(false)| before sending a signal.
class TestSignalHandler final {
 public:
  void set_result(bool result) { result_ = result; }

  bool OnSignal(zx_signals_t observed) {
    observed_ |= observed;
    sync_completion_signal(&sync_);
    return result_;
  }

  zx_signals_t WaitOne() {
    sync_completion_wait(&sync_, ZX_TIME_INFINITE);
    sync_completion_reset(&sync_);
    return observed_.exchange(0);
  }

 private:
  std::atomic<zx_signals_t> observed_ = 0;
  sync_completion_t sync_;
  bool result_ = true;
};

class SignalCoordinatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    zx::eventpair paired;
    coordinator1_.Create(&paired, [&](zx_signals_t s) { return handler1_.OnSignal(s); });
    coordinator2_.Pair(std::move(paired), [&](zx_signals_t s) { return handler2_.OnSignal(s); });
  }

  TestSignalHandler handler1_;
  TestSignalHandler handler2_;
  SignalCoordinator coordinator1_;
  SignalCoordinator coordinator2_;
};

// Unit tests

TEST_F(SignalCoordinatorTest, JoinAndReset) {
  // |Join| will block until the coordinator is stopped. |Reset| stops the object and its peer.
  std::thread t1([&]() { coordinator1_.Join(); });
  coordinator2_.Reset();
  t1.join();

  // |Join| does not block if already stopped. |Reset| is idempotent.
  coordinator1_.Join();
  coordinator2_.Reset();
  coordinator2_.Join();
}

TEST_F(SignalCoordinatorTest, AutoReset) {
  // Re-creating the coordinator will reset it, and stop its peer.
  zx::eventpair paired1;
  std::thread t1([&]() { coordinator1_.Join(); });
  coordinator2_.Create(&paired1, [&](zx_signals_t s) { return handler2_.OnSignal(s); });
  t1.join();
  coordinator1_.Pair(std::move(paired1), [&](zx_signals_t s) { return handler1_.OnSignal(s); });

  // Similarly, re-pairing also resets.
  zx::eventpair paired2a, paired2b;
  ASSERT_EQ(zx::eventpair::create(0, &paired2a, &paired2b), ZX_OK);
  std::thread t2([&]() { coordinator2_.Join(); });
  coordinator1_.Pair(std::move(paired2a), [&](zx_signals_t s) { return handler1_.OnSignal(s); });
  t2.join();
}

TEST_F(SignalCoordinatorTest, WaitLoop) {
  // Can send all signals, both ways.
  std::vector<Signal> signals = {
      kExecuteCallback, kCollectCoverage, kTryDetectingALeak, kLeakDetected, kDetectLeaksAtExit,
  };
  std::thread t;
  for (auto signal : signals) {
    t = std::thread([&]() { EXPECT_EQ(handler2_.WaitOne(), signal); });
    EXPECT_TRUE(coordinator1_.SignalPeer(signal));
    t.join();

    t = std::thread([&]() { EXPECT_EQ(handler1_.WaitOne(), signal); });
    EXPECT_TRUE(coordinator2_.SignalPeer(signal));
    t.join();
  }
}

TEST_F(SignalCoordinatorTest, PeerClosed) {
  // This will cause |TestSignalHandler::OnSignal| to return false, which tells the
  // |SignalCoordinator::WaitLoop| to exit.
  handler2_.set_result(false);
  std::thread t([&]() {
    auto observed = handler2_.WaitOne();
    // If the signal handler returns false, it's called again with ZX_EVENTPAIR_PEER_CLOSED.
    // Either one or both calls happen before the call to |WaitOne| above returns.
    if (observed == kExecuteCallback) {
      EXPECT_EQ(handler2_.WaitOne(), ZX_EVENTPAIR_PEER_CLOSED);
    } else {
      EXPECT_EQ(observed, kExecuteCallback | ZX_EVENTPAIR_PEER_CLOSED);
    }
  });

  EXPECT_TRUE(coordinator1_.SignalPeer(kExecuteCallback));
  t.join();

  // After ZX_EVENTPAIR_PEER_CLOSED, the coordinator stops...
  coordinator2_.Join();

  // ...which causes the other end to receive ZX_EVENTPAIR_PEER_CLOSED and stop.
  EXPECT_EQ(handler1_.WaitOne(), ZX_EVENTPAIR_PEER_CLOSED);
  coordinator1_.Join();

  // Once stopped, can't send more signals.
  EXPECT_FALSE(coordinator1_.SignalPeer(kExecuteCallback));
}

}  // namespace
}  // namespace fuzzing
