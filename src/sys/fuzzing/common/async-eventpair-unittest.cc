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

TEST_F(AsyncEventPairTest, SignalSelf) {
  AsyncEventPair eventpair1(executor());
  AsyncEventPair eventpair2(executor());
  eventpair1.Pair(eventpair2.Create());

  eventpair1.SignalSelf(0, ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), ZX_USER_SIGNAL_1);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_ALL), ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_ALL), 0U);

  eventpair1.SignalSelf(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, ZX_USER_SIGNAL_3);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), 0U);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_ALL), ZX_USER_SIGNAL_2 | ZX_USER_SIGNAL_3);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_ALL), 0U);
}

TEST_F(AsyncEventPairTest, SignalPeer) {
  AsyncEventPair eventpair1(executor());
  AsyncEventPair eventpair2(executor());
  eventpair1.Pair(eventpair2.Create());

  eventpair2.SignalPeer(0, ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), ZX_USER_SIGNAL_1);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_ALL), ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_ALL), 0U);

  eventpair2.SignalPeer(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, ZX_USER_SIGNAL_3);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_1), 0U);
  EXPECT_EQ(eventpair1.GetSignals(ZX_USER_SIGNAL_ALL), ZX_USER_SIGNAL_2 | ZX_USER_SIGNAL_3);
  EXPECT_EQ(eventpair2.GetSignals(ZX_USER_SIGNAL_ALL), 0U);
}

TEST_F(AsyncEventPairTest, WaitFor) {
  AsyncEventPair eventpair1(executor());
  AsyncEventPair eventpair2(executor());
  eventpair1.Pair(eventpair2.Create());

  auto signals01 = ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1;
  FUZZING_EXPECT_OK(eventpair1.WaitFor(signals01), signals01);
  eventpair2.SignalPeer(0, signals01);
  RunUntilIdle();

  // Should be able to return immediately, and get back only requested signals.
  auto signals1 = ZX_USER_SIGNAL_1;
  FUZZING_EXPECT_OK(eventpair1.WaitFor(signals1), signals1);
  RunUntilIdle();

  // Can clear and be signaled again.
  eventpair1.SignalSelf(signals1, 0);
  auto signals12 = ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2;
  FUZZING_EXPECT_OK(eventpair1.WaitFor(signals12), signals12);
  eventpair2.SignalPeer(0, signals12);
  RunUntilIdle();
}

}  // namespace fuzzing
