// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zxtest/zxtest.h>

namespace {

zx_signals_t GetPendingSignals(const zx::eventpair& eventpair) {
  zx_signals_t pending = 0;

  EXPECT_STATUS(ZX_ERR_TIMED_OUT, eventpair.wait_one(0, zx::time::infinite_past(), &pending));

  return pending;
}

TEST(EventPairTest, HandlesNotInvalid) {
  zx::eventpair eventpair_0, eventpair_1;

  ASSERT_OK(zx::eventpair::create(0, &eventpair_0, &eventpair_1));

  EXPECT_NE(eventpair_0.get(), ZX_HANDLE_INVALID);
  EXPECT_NE(eventpair_1.get(), ZX_HANDLE_INVALID);
}

TEST(EventPairTest, HandleRightsAreCorrect) {
  zx::eventpair eventpair_0, eventpair_1;

  ASSERT_OK(zx::eventpair::create(0, &eventpair_0, &eventpair_1));

  zx_info_handle_basic_t info = {};
  ASSERT_OK(eventpair_0.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rights, ZX_RIGHTS_BASIC | ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER);
  EXPECT_EQ(info.type, static_cast<uint32_t>(ZX_OBJ_TYPE_EVENTPAIR));

  info = {};
  ASSERT_OK(eventpair_1.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rights, ZX_RIGHTS_BASIC | ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER);
  EXPECT_EQ(info.type, static_cast<uint32_t>(ZX_OBJ_TYPE_EVENTPAIR));
}

TEST(EventPairTest, KoidsAreCorrect) {
  zx::eventpair eventpair_0, eventpair_1;

  ASSERT_OK(zx::eventpair::create(0, &eventpair_0, &eventpair_1));

  zx_info_handle_basic_t info_0 = {};
  zx_info_handle_basic_t info_1 = {};
  ASSERT_OK(eventpair_0.get_info(ZX_INFO_HANDLE_BASIC, &info_0, sizeof(info_0), nullptr, nullptr));
  ASSERT_OK(eventpair_1.get_info(ZX_INFO_HANDLE_BASIC, &info_1, sizeof(info_1), nullptr, nullptr));

  // Check that koids line up.
  EXPECT_NE(info_0.koid, 0);
  EXPECT_NE(info_0.related_koid, 0);
  EXPECT_NE(info_1.koid, 0);
  EXPECT_NE(info_1.related_koid, 0);
  EXPECT_EQ(info_0.koid, info_1.related_koid);
  EXPECT_EQ(info_1.koid, info_0.related_koid);
}

// Currently no flags are supported.
TEST(EventPairTest, CheckNoFlagsSupported) {
  zx::eventpair eventpair_0, eventpair_1;

  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zx::eventpair::create(1, &eventpair_0, &eventpair_1));

  EXPECT_EQ(eventpair_0.get(), ZX_HANDLE_INVALID);
  EXPECT_EQ(eventpair_1.get(), ZX_HANDLE_INVALID);
}

TEST(EventPairTest, SignalEventPairAndClearVerifySignals) {
  zx::eventpair eventpair_0, eventpair_1;

  ASSERT_OK(zx::eventpair::create(0, &eventpair_0, &eventpair_1));

  EXPECT_EQ(GetPendingSignals(eventpair_0), 0);
  EXPECT_EQ(GetPendingSignals(eventpair_1), 0);

  ASSERT_OK(eventpair_0.signal(0, ZX_USER_SIGNAL_0));
  EXPECT_EQ(GetPendingSignals(eventpair_0), ZX_USER_SIGNAL_0);
  EXPECT_EQ(GetPendingSignals(eventpair_1), 0);

  ASSERT_OK(eventpair_0.signal(ZX_USER_SIGNAL_0, 0));
  EXPECT_EQ(GetPendingSignals(eventpair_1), 0);
  EXPECT_EQ(GetPendingSignals(eventpair_0), 0);
}

TEST(EventPairTest, SignalPeerAndVerifyRecived) {
  zx::eventpair eventpair_0, eventpair_1;

  ASSERT_OK(zx::eventpair::create(0, &eventpair_0, &eventpair_1));

  ASSERT_OK(eventpair_0.signal_peer(0, ZX_USER_SIGNAL_0));
  EXPECT_EQ(GetPendingSignals(eventpair_0), 0);
  EXPECT_EQ(GetPendingSignals(eventpair_1), ZX_USER_SIGNAL_0);

  ASSERT_OK(eventpair_1.signal_peer(0, ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2));
  EXPECT_EQ(GetPendingSignals(eventpair_0), ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2);
  EXPECT_EQ(GetPendingSignals(eventpair_1), ZX_USER_SIGNAL_0);

  ASSERT_OK(eventpair_0.signal_peer(ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_3 | ZX_USER_SIGNAL_4));
  EXPECT_EQ(GetPendingSignals(eventpair_0), ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2);
  EXPECT_EQ(GetPendingSignals(eventpair_1), ZX_USER_SIGNAL_3 | ZX_USER_SIGNAL_4);
}

TEST(EventPairTest, SignalPeerThenCloseAndVerifySignalReceived) {
  zx::eventpair eventpair_0, eventpair_1;

  ASSERT_OK(zx::eventpair::create(0, &eventpair_0, &eventpair_1));

  ASSERT_OK(eventpair_0.signal_peer(0, ZX_USER_SIGNAL_3 | ZX_USER_SIGNAL_4));

  eventpair_0.reset();

  // Signaled flags should remain satisfied but now should now also get peer closed (and
  // unsignaled flags should be unsatisfiable).
  EXPECT_EQ(GetPendingSignals(eventpair_1),
            ZX_EVENTPAIR_PEER_CLOSED | ZX_USER_SIGNAL_3 | ZX_USER_SIGNAL_4);
}

TEST(EventPairTest, SignalingClosedPeerReturnsPeerClosed) {
  zx::eventpair eventpair_0, eventpair_1;

  ASSERT_OK(zx::eventpair::create(0, &eventpair_0, &eventpair_1));

  eventpair_1.reset();
  EXPECT_STATUS(ZX_ERR_PEER_CLOSED, eventpair_0.signal_peer(0, ZX_USER_SIGNAL_0));
}

}  // namespace
