// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "enhanced_retransmission_mode_rx_engine.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fragmenter.h"
#include "gtest/gtest.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace {

constexpr hci::ConnectionHandle kTestHandle = 0x0001;
constexpr ChannelId kTestChannelId = 0x0001;

using Engine = EnhancedRetransmissionModeRxEngine;

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduImmediatelyReturnsDataForUnsegmentedSdu) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  const auto payload =
      common::CreateStaticByteBuffer(0, 0, 'h', 'e', 'l', 'l', 'o');
  const common::ByteBufferPtr sdu = Engine().ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload));
  ASSERT_TRUE(sdu);
  EXPECT_TRUE(common::ContainersEqual(
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o'), *sdu));
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduCanHandleZeroBytePayload) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  const auto payload = common::CreateStaticByteBuffer(0, 0);
  const common::ByteBufferPtr sdu = Engine().ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload));
  ASSERT_TRUE(sdu);
  EXPECT_EQ(0u, sdu->size());
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduDoesNotGenerateSduForOutOfSequencePdu) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2 for the first two bytes.
  const auto payload = common::CreateStaticByteBuffer(  //
      1 << 1,                                           // TxSeq = 1, R=0
      0,                                                // SAR and ReqSeq
      'h', 'e', 'l', 'l', 'o');
  EXPECT_FALSE(Engine().ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduAdvancesSequenceNumberOnInSequenceFrame) {
  Engine rx_engine;

  // Send with sequence 0.
  {
    const auto payload = common::CreateStaticByteBuffer(  //
        0 << 1,                                           // TxSeq=0, R=0
        0,                                                // SAR and ReqSeq
        'h', 'e', 'l', 'l', 'o');
    ASSERT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
  }

  // Send with sequence 1.
  {
    const auto payload = common::CreateStaticByteBuffer(  //
        1 << 1,                                           // TxSeq=1, R=0
        0,                                                // SAR and ReqSeq
        'h', 'e', 'l', 'l', 'o');
    ASSERT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
  }

  // Send with sequence 2.
  {
    const auto payload = common::CreateStaticByteBuffer(  //
        2 << 1,                                           // TxSeq=2, R=0
        0,                                                // SAR and ReqSeq
        'h', 'e', 'l', 'l', 'o');
    EXPECT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
  }
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduRollsOverSequenceNumber) {
  Engine rx_engine;
  auto payload = common::CreateStaticByteBuffer(  //
      0 << 1,                                     // TxSeq=0, R=0
      0,                                          // SAR and ReqSeq
      'h', 'e', 'l', 'l', 'o');
  for (size_t i = 0; i < 64; ++i) {
    payload[0] = i << 1;  // Set TxSeq
    ASSERT_TRUE(rx_engine.ProcessPdu(
        Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)))
        << " (i=" << i << ")";
  }

  // Per Core Spec v5, Vol 3, Part A, Sec 8.3, the sequence number should now
  // roll over to 0.
  payload[0] = 0 << 1;
  EXPECT_TRUE(rx_engine.ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, payload)));
}

TEST(L2CAP_EnhancedRetransmissionModeRxEngineTest,
     ProcessPduDoesNotAdvanceSequenceNumberForOutOfSequencePdu) {
  Engine rx_engine;
  const auto out_of_seq = common::CreateStaticByteBuffer(  //
      1 << 1,                                              // TxSeq=1, R=0
      0,                                                   // SAR and ReqSeq
      'h', 'e', 'l', 'l', 'o');
  ASSERT_FALSE(rx_engine.ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, out_of_seq)));

  const auto in_seq = common::CreateStaticByteBuffer(  //
      0 << 1,                                          // TxSeq=0, R=0
      0,                                               // SAR and ReqSeq
      'h', 'e', 'l', 'l', 'o');
  EXPECT_TRUE(rx_engine.ProcessPdu(
      Fragmenter(kTestHandle).BuildBasicFrame(kTestChannelId, in_seq)));
}

}  // namespace
}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
