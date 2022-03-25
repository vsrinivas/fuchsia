// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/async-eventpair.h"

#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Test fixture.

class AsyncEventPairTest : public AsyncTest {};

// Unit tests.

TEST_F(AsyncEventPairTest, Create) {
  AsyncEventPair eventpair(executor());

  auto ep1 = eventpair.Create();
  EXPECT_EQ(ep1.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr),
            ZX_ERR_TIMED_OUT);

  // Creating again closes the previous eventpair.
  auto ep2 = eventpair.Create();
  EXPECT_EQ(ep1.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr), ZX_OK);
}

TEST_F(AsyncEventPairTest, Pair) {
  AsyncEventPair eventpair(executor());

  zx::eventpair ep1, ep2;
  EXPECT_EQ(zx::eventpair::create(0, &ep1, &ep2), ZX_OK);
  eventpair.Pair(std::move(ep1));
  EXPECT_EQ(ep2.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr),
            ZX_ERR_TIMED_OUT);

  // Pairing again closes the previous eventpair.
  zx::eventpair ep3, ep4;
  EXPECT_EQ(zx::eventpair::create(0, &ep3, &ep4), ZX_OK);
  eventpair.Pair(std::move(ep3));
  EXPECT_EQ(ep2.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr), ZX_OK);
}

TEST_F(AsyncEventPairTest, IsConnected) {
  AsyncEventPair eventpair1(executor());
  AsyncEventPair eventpair2(executor());
  EXPECT_FALSE(eventpair1.IsConnected());
  EXPECT_FALSE(eventpair2.IsConnected());

  auto eventpair = eventpair2.Create();
  EXPECT_FALSE(eventpair1.IsConnected());
  EXPECT_TRUE(eventpair2.IsConnected());

  eventpair1.Pair(std::move(eventpair));
  EXPECT_TRUE(eventpair1.IsConnected());
  EXPECT_TRUE(eventpair2.IsConnected());

  eventpair2.Reset();
  EXPECT_FALSE(eventpair1.IsConnected());
  EXPECT_FALSE(eventpair2.IsConnected());
}

TEST_F(AsyncEventPairTest, SignalSelf) {
  AsyncEventPair eventpair1(executor());
  AsyncEventPair eventpair2(executor());

  // Returns error if not connected.
  EXPECT_EQ(eventpair1.SignalSelf(0, ZX_USER_SIGNAL_1), ZX_ERR_PEER_CLOSED);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), 0U);
  eventpair1.Pair(eventpair2.Create());

  EXPECT_EQ(eventpair1.SignalSelf(0, ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2), ZX_OK);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), ZX_USER_SIGNAL_1);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_ALL), ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_ALL), 0U);

  EXPECT_EQ(eventpair1.SignalSelf(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, ZX_USER_SIGNAL_3), ZX_OK);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), 0U);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_ALL), ZX_USER_SIGNAL_2 | ZX_USER_SIGNAL_3);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_ALL), 0U);

  // Can only raise user signals.
  EXPECT_EQ(eventpair1.SignalSelf(0, ZX_USER_SIGNAL_0 | ZX_EVENTPAIR_PEER_CLOSED), ZX_OK);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_0 | ZX_EVENTPAIR_PEER_CLOSED), ZX_USER_SIGNAL_0);
}

TEST_F(AsyncEventPairTest, SignalPeer) {
  AsyncEventPair eventpair1(executor());
  AsyncEventPair eventpair2(executor());

  // Returns error if not connected.
  EXPECT_EQ(eventpair1.SignalPeer(0, ZX_USER_SIGNAL_1), ZX_ERR_PEER_CLOSED);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_1), 0U);
  eventpair1.Pair(eventpair2.Create());

  EXPECT_EQ(eventpair2.SignalPeer(0, ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2), ZX_OK);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), ZX_USER_SIGNAL_1);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_ALL), ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_ALL), 0U);

  EXPECT_EQ(eventpair2.SignalPeer(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, ZX_USER_SIGNAL_3), ZX_OK);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), 0U);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_ALL), ZX_USER_SIGNAL_2 | ZX_USER_SIGNAL_3);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_ALL), 0U);

  // Can only raise user signals.
  EXPECT_EQ(eventpair2.SignalPeer(0, ZX_USER_SIGNAL_0 | ZX_EVENTPAIR_PEER_CLOSED), ZX_OK);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_0 | ZX_EVENTPAIR_PEER_CLOSED), ZX_USER_SIGNAL_0);
}

TEST_F(AsyncEventPairTest, WaitFor) {
  AsyncEventPair eventpair1(executor());
  AsyncEventPair eventpair2(executor());

  // Returns error if not connected.
  auto signals01 = ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1;
  FUZZING_EXPECT_ERROR(eventpair1.WaitFor(signals01), ZX_ERR_PEER_CLOSED);
  RunUntilIdle();
  eventpair1.Pair(eventpair2.Create());

  FUZZING_EXPECT_OK(eventpair1.WaitFor(signals01), signals01);
  FUZZING_EXPECT_OK(executor()->MakeDelayedPromise(zx::msec(1)).and_then([&] {
    EXPECT_EQ(eventpair2.SignalPeer(0, signals01), ZX_OK);
    return fpromise::ok();
  }));
  RunUntilIdle();

  // Should be able to return immediately, and get back only requested signals.
  auto signals1 = ZX_USER_SIGNAL_1;
  FUZZING_EXPECT_OK(eventpair1.WaitFor(signals1), signals1);
  RunUntilIdle();

  // Can clear and be signaled again.
  EXPECT_EQ(eventpair1.SignalSelf(signals1, 0), ZX_OK);
  auto signals12 = ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2;
  FUZZING_EXPECT_OK(eventpair1.WaitFor(signals12), signals12);
  FUZZING_EXPECT_OK(executor()->MakeDelayedPromise(zx::msec(1)).and_then([&] {
    EXPECT_EQ(eventpair2.SignalPeer(0, signals12), ZX_OK);
    return fpromise::ok();
  }));
  RunUntilIdle();

  // Wait should be interrupted by a local |Reset|.
  EXPECT_EQ(eventpair1.SignalSelf(signals1, 0), ZX_OK);
  FUZZING_EXPECT_ERROR(eventpair1.WaitFor(signals1));
  FUZZING_EXPECT_OK(executor()->MakeDelayedPromise(zx::msec(1)).and_then([&] {
    eventpair1.Reset();
    return fpromise::ok();
  }));
  RunUntilIdle();

  // Waiting after local |Reset| immediately returns error.
  FUZZING_EXPECT_ERROR(eventpair1.WaitFor(signals1));
  RunUntilIdle();

  // Wait should be interrupted by a remote |Reset|.
  eventpair1.Pair(eventpair2.Create());
  EXPECT_EQ(eventpair1.SignalSelf(signals1, 0), ZX_OK);
  FUZZING_EXPECT_ERROR(eventpair1.WaitFor(signals1));
  FUZZING_EXPECT_OK(executor()->MakeDelayedPromise(zx::msec(1)).and_then([&] {
    eventpair2.Reset();
    return fpromise::ok();
  }));
  RunUntilIdle();

  // Waiting after remote |Reset| immediately returns error.
  FUZZING_EXPECT_ERROR(eventpair1.WaitFor(signals1));
  RunUntilIdle();

  // Dropping the remote end returns an error.
  {
    auto eventpair = eventpair1.Create();
    FUZZING_EXPECT_ERROR(eventpair1.WaitFor(signals1));
  }
  RunUntilIdle();
}

}  // namespace fuzzing
