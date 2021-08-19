// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/signal-coordinator.h"

#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <atomic>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/testing/signal-coordinator.h"

namespace fuzzing {
namespace {

TEST(SignalCoordinatorTest, JoinAndReset) {
  SignalCoordinator coordinator1, coordinator2;

  auto paired = coordinator1.Create([](zx_signals_t s) { return true; });
  coordinator2.Pair(std::move(paired), [](zx_signals_t s) { return true; });

  // |Join| will block until the coordinator is stopped. |Reset| stops the object and its peer.
  std::thread t1([&]() { coordinator1.Join(); });
  coordinator2.Reset();
  t1.join();

  // |Join| does not block if already stopped. |Reset| is idempotent.
  coordinator1.Join();
  coordinator2.Reset();
  coordinator2.Join();
}

TEST(SignalCoordinatorTest, AutoReset) {
  SignalCoordinator coordinator1, coordinator2;

  auto paired = coordinator1.Create([](zx_signals_t s) { return true; });
  coordinator2.Pair(std::move(paired), [](zx_signals_t s) { return true; });

  // Re-creating the coordinator will reset it, and stop its peer.
  std::thread t1([&]() { coordinator1.Join(); });
  auto paired1 = coordinator2.Create([](zx_signals_t s) { return true; });
  t1.join();
  coordinator1.Pair(std::move(paired1), [](zx_signals_t s) { return true; });

  // Similarly, re-pairing also resets.
  zx::eventpair paired2a, paired2b;
  ASSERT_EQ(zx::eventpair::create(0, &paired2a, &paired2b), ZX_OK);
  std::thread t2([&]() { coordinator2.Join(); });
  coordinator1.Pair(std::move(paired2a), [](zx_signals_t s) { return true; });
  t2.join();
}

TEST(SignalCoordinatorTest, WaitLoop) {
  FakeSignalCoordinator coordinator1, coordinator2;
  coordinator2.Pair(coordinator1.Create());

  // Can send all signals, both ways.
  std::vector<Signal> signals = {
      kExecuteCallback, kCollectCoverage, kTryDetectingALeak, kLeakDetected, kDetectLeaksAtExit,
  };
  std::thread t;
  for (auto signal : signals) {
    t = std::thread([&]() { EXPECT_EQ(coordinator2.AwaitSignal(), signal); });
    EXPECT_TRUE(coordinator1.SignalPeer(signal));
    t.join();

    t = std::thread([&]() { EXPECT_EQ(coordinator1.AwaitSignal(), signal); });
    EXPECT_TRUE(coordinator2.SignalPeer(signal));
    t.join();
  }
}

TEST(SignalCoordinatorTest, PeerClosed) {
  FakeSignalCoordinator coordinator1, coordinator2;
  coordinator2.Pair(coordinator1.Create());

  std::thread t([&]() {
    auto observed = coordinator2.AwaitSignal();
    // Signals may arrive separately or combined.
    if (observed == kExecuteCallback) {
      EXPECT_EQ(coordinator2.AwaitSignal(), ZX_EVENTPAIR_PEER_CLOSED);
    } else {
      EXPECT_NE(observed & ZX_EVENTPAIR_PEER_CLOSED, 0U);
    }
  });

  EXPECT_TRUE(coordinator1.SignalPeer(kExecuteCallback));
  coordinator1.Reset();
  t.join();

  // After ZX_EVENTPAIR_PEER_CLOSED, the coordinator stops...
  coordinator2.Join();

  // Once stopped, can't send more signals.
  EXPECT_FALSE(coordinator1.SignalPeer(kExecuteCallback));
}

}  // namespace
}  // namespace fuzzing
