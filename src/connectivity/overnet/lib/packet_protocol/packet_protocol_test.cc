// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/packet_protocol/packet_protocol.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/packet_protocol/aead_codec.h"
#include "src/connectivity/overnet/lib/packet_protocol/packet_protocol_fuzzer.h"
#include "src/connectivity/overnet/lib/protocol/serialization_helpers.h"
#include "src/connectivity/overnet/lib/testing/flags.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"
#include "src/connectivity/overnet/lib/vocabulary/closed_ptr.h"

using testing::_;
using testing::Mock;
using testing::Pointee;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace packet_protocol_test {

// Some default MSS for tests
static const uint64_t kMSS = 1500;

class MockPacketSender : public PacketProtocol::PacketSender {
 public:
  MOCK_METHOD2(SendPacketMock, void(SeqNum, Slice));

  void SendPacket(SeqNum seq, LazySlice slice) override {
    SendPacketMock(seq, slice(LazySliceArgs{Border::None(), kMSS, false}));
  }

  MOCK_METHOD1(SendCallback, void(const Status&));

  auto NewSendCallback() {
    return [this](const Status& status) { this->SendCallback(status); };
  }
};

class DummyCodec final : public PacketProtocol::Codec {
 public:
  DummyCodec() : Codec(Border{9, 9}) {}
  StatusOr<Slice> Encode(uint64_t seq_idx, Slice slice) const {
    return Slice::WithInitializer(slice.length() + 18, [&](uint8_t* p) {
      *p++ = '[';
      p = WriteLE64(seq_idx, p);
      memcpy(p, slice.begin(), slice.length());
      p += slice.length();
      p = WriteLE64(seq_idx, p);
      *p++ = ']';
    });
  }
  StatusOr<Slice> Decode(uint64_t seq_idx, Slice slice) const {
    const uint8_t* p = slice.begin();
    EXPECT_LT(p, slice.end());
    EXPECT_EQ(*p, '[');
    p++;
    uint64_t seq1;
    EXPECT_TRUE(ParseLE64(&p, slice.end(), &seq1));
    EXPECT_EQ(seq_idx, seq1);
    EXPECT_GE(slice.length(), 9ull);
    p = slice.end() - 9;
    uint64_t seq2;
    EXPECT_TRUE(ParseLE64(&p, slice.end(), &seq2));
    EXPECT_EQ(seq_idx, seq2);
    EXPECT_EQ(*p, ']');
    return slice.Cut(9, slice.length() - 9);
  }
};
static DummyCodec dummy_codec;

static std::unique_ptr<AEADCodec> MakeTestAEADCodec(const EVP_AEAD* fn) {
  std::vector<uint8_t> key;
  std::random_device rng;
  for (size_t i = 0; i < EVP_AEAD_key_length(fn); i++) {
    key.push_back(rng());
  }

  return std::make_unique<AEADCodec>(fn, key.data(), key.size(), nullptr, 0);
}
static std::unique_ptr<AEADCodec> aead_codec_chacha =
    MakeTestAEADCodec(EVP_aead_chacha20_poly1305());
static std::unique_ptr<AEADCodec> aead_codec_hmac =
    MakeTestAEADCodec(EVP_aead_aes_256_ctr_hmac_sha256());

class PacketProtocolTest
    : public ::testing::TestWithParam<const PacketProtocol::Codec*> {};

TEST_P(PacketProtocolTest, NoOp) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};

  StrictMock<MockPacketSender> ps;
  std::mt19937 rng{123};
  MakeClosedPtr<PacketProtocol>(
      &timer, [&rng] { return rng(); }, &ps, GetParam(), kMSS, true);
}

TEST_P(PacketProtocolTest, SendOnePacket) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};

  StrictMock<MockPacketSender> ps;
  std::mt19937 rng{123};
  auto packet_protocol = MakeClosedPtr<PacketProtocol>(
      &timer, [&rng] { return rng(); }, &ps, GetParam(), kMSS, true);

  // Send some dummy data: we expect to see a packet emitted immediately
  Slice got_slice;
  EXPECT_CALL(
      ps, SendPacketMock(Property(&SeqNum::ReconstructFromZero_TestOnly, 1), _))
      .WillOnce(SaveArg<1>(&got_slice));

  packet_protocol->Send(
      [](auto arg) {
        EXPECT_LT(arg.desired_border.prefix, kMSS);
        EXPECT_LT(arg.desired_border.suffix, kMSS);
        EXPECT_LE(arg.max_length, kMSS);
        EXPECT_GT(arg.max_length, uint64_t(0));
        return Slice::FromContainer({1, 2, 3, 4, 5});
      },
      ps.NewSendCallback());
  Mock::VerifyAndClearExpectations(&ps);

  auto status = GetParam()->Decode(1, got_slice);
  EXPECT_TRUE(status.is_ok());
  EXPECT_EQ(*status, Slice::FromContainer({0, 1, 2, 3, 4, 5}));

  // Build a fake ack packet.
  Slice ack = Slice::FromWritable(AckFrame(1, 1));
  status = GetParam()->Encode(
      1, ack.WithPrefix(1, [len = ack.length()](uint8_t* p) { *p = len; }));
  EXPECT_TRUE(status.is_ok());
  ack = std::move(*status);

  // Calling Process on it should succeed and trigger the completion callback
  // for the send.
  EXPECT_CALL(ps, SendCallback(Property(&Status::is_ok, true)));
  packet_protocol->Process(TimeStamp::Epoch(), SeqNum(1, 1), ack,
                           [](auto status) {
                             EXPECT_TRUE(status.is_ok()) << status.AsStatus();
                             EXPECT_EQ(*status, nullptr);
                           });
}

// Exposed some bugs in the fuzzer, and a bug whereby empty ack frames caused a
// failure.
TEST_P(PacketProtocolTest, _02ef5d596c101ce01181a7dcd0a294ed81c88dbd) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 1); }))) {
    return;
  }
  static const uint8_t block1[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 1); }))) {
    return;
  }
  static const uint8_t block2[] = {0x01, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 2); }))) {
    return;
  }
  static const uint8_t block3[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 1); }))) {
    return;
  }
  static const uint8_t block4[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(3),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 1); }))) {
    return;
  }
  static const uint8_t block6[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 1); }))) {
    return;
  }
  static const uint8_t block7[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block7, 1); }))) {
    return;
  }
  static const uint8_t block8[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(255),
                               [](uint8_t* p) { memcpy(p, block8, 1); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

// Exposed a bug in the fuzzer.
TEST_P(PacketProtocolTest, _d9c8d575a34f511dfae936725f8a6752b910e258) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 1); }))) {
    return;
  }
  static const uint8_t block1[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 1); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 1)) {
    return;
  }
  static const uint8_t block2[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 1); }))) {
    return;
  }
  static const uint8_t block3[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 1); }))) {
    return;
  }
  static const uint8_t block4[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  if (!fuzzer.StepTime(506ull)) {
    return;
  }
  if (!fuzzer.StepTime(1ull)) {
    return;
  }
  static const uint8_t block5[] = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0xfa, 0x03,
      0x03, 0xfa, 0xff, 0xff, 0x03, 0x03, 0xfa, 0xff, 0xff, 0x01, 0x02, 0xff,
      0xff, 0xff, 0x01, 0xff, 0xff, 0x00, 0x54, 0xff, 0x28, 0xff, 0xff, 0x97,
      0xff, 0xff, 0xff, 0xff, 0xe0, 0xff, 0xff, 0xbb, 0x01, 0x00, 0xff, 0x00,
      0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc2, 0xff, 0xff, 0x34, 0x34,
      0x34, 0x34, 0x34, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0xff, 0xff, 0x03, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               250, Border::Prefix(255),
                               [](uint8_t* p) { memcpy(p, block5, 250); }))) {
    return;
  }
}

// Found a bug with ack ordering on writes.
TEST_P(PacketProtocolTest, _da3f40d81b8c5d0609dc83e2edeb576054c106b8) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 1); }))) {
    return;
  }
  static const uint8_t block1[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 1); }))) {
    return;
  }
  static const uint8_t block2[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 1); }))) {
    return;
  }
  static const uint8_t block3[] = {0xee};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 1); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0xee};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(0, 0)) {
    return;
  }
}

// Found a wraparound bug in tiemr code.
TEST_P(PacketProtocolTest, _87b138d6497b8f037691af618f319455c6f5a3b0) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x01, 0x01, 0x01, 0x01, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               5, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 5); }))) {
    return;
  }
  static const uint8_t block1[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 1); }))) {
    return;
  }
  static const uint8_t block2[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 1); }))) {
    return;
  }
  static const uint8_t block3[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 1); }))) {
    return;
  }
  static const uint8_t block4[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 1); }))) {
    return;
  }
  static const uint8_t block6[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 1); }))) {
    return;
  }
  static const uint8_t block7[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block7, 1); }))) {
    return;
  }
  static const uint8_t block8[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block8, 1); }))) {
    return;
  }
  static const uint8_t block9[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block9, 1); }))) {
    return;
  }
  static const uint8_t block10[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block10, 1); }))) {
    return;
  }
  static const uint8_t block11[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block11, 1); }))) {
    return;
  }
  static const uint8_t block12[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block12, 1); }))) {
    return;
  }
  static const uint8_t block13[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block13, 1); }))) {
    return;
  }
  static const uint8_t block14[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block14, 1); }))) {
    return;
  }
  static const uint8_t block15[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block15, 1); }))) {
    return;
  }
  static const uint8_t block16[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block16, 1); }))) {
    return;
  }
  static const uint8_t block17[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block17, 1); }))) {
    return;
  }
  static const uint8_t block18[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block18, 1); }))) {
    return;
  }
  static const uint8_t block19[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block19, 1); }))) {
    return;
  }
  static const uint8_t block20[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block20, 1); }))) {
    return;
  }
  static const uint8_t block21[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block21, 1); }))) {
    return;
  }
  static const uint8_t block22[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block22, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block23[] = {0x02, 0x00};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block23, 2); }))) {
    return;
  }
  static const uint8_t block24[] = {0x01};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block24, 1); }))) {
    return;
  }
  static const uint8_t block25[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block25, 1); }))) {
    return;
  }
  static const uint8_t block26[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block26, 1); }))) {
    return;
  }
  static const uint8_t block27[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block27, 1); }))) {
    return;
  }
  static const uint8_t block28[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block28, 1); }))) {
    return;
  }
  static const uint8_t block29[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block29, 1); }))) {
    return;
  }
  static const uint8_t block30[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block30, 1); }))) {
    return;
  }
  static const uint8_t block31[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block31, 1); }))) {
    return;
  }
  static const uint8_t block32[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block32, 1); }))) {
    return;
  }
  static const uint8_t block33[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block33, 1); }))) {
    return;
  }
  static const uint8_t block34[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block34, 1); }))) {
    return;
  }
  static const uint8_t block35[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block35, 1); }))) {
    return;
  }
  static const uint8_t block36[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block36, 1); }))) {
    return;
  }
  static const uint8_t block37[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block37, 1); }))) {
    return;
  }
  static const uint8_t block38[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block38, 1); }))) {
    return;
  }
  static const uint8_t block39[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block39, 1); }))) {
    return;
  }
  static const uint8_t block40[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block40, 1); }))) {
    return;
  }
  static const uint8_t block41[] = {
      0xfe, 0xfa, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0xff, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xff,
      0x07, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               255, Border::Prefix(254),
                               [](uint8_t* p) { memcpy(p, block41, 255); }))) {
    return;
  }
  static const uint8_t block42[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block42, 1); }))) {
    return;
  }
  static const uint8_t block43[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block43, 1); }))) {
    return;
  }
  static const uint8_t block44[] = {0x01};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block44, 1); }))) {
    return;
  }
  static const uint8_t block45[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block45, 1); }))) {
    return;
  }
  static const uint8_t block46[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block46, 1); }))) {
    return;
  }
  static const uint8_t block47[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block47, 1); }))) {
    return;
  }
  static const uint8_t block48[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block48, 1); }))) {
    return;
  }
  static const uint8_t block49[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block49, 1); }))) {
    return;
  }
  static const uint8_t block50[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block50, 1); }))) {
    return;
  }
  static const uint8_t block51[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block51, 1); }))) {
    return;
  }
  static const uint8_t block52[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block52, 1); }))) {
    return;
  }
  static const uint8_t block53[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block53, 1); }))) {
    return;
  }
  static const uint8_t block54[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block54, 1); }))) {
    return;
  }
  static const uint8_t block55[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block55, 1); }))) {
    return;
  }
  static const uint8_t block56[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block56, 1); }))) {
    return;
  }
  static const uint8_t block57[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block57, 1); }))) {
    return;
  }
  static const uint8_t block58[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block58, 1); }))) {
    return;
  }
  static const uint8_t block59[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block59, 1); }))) {
    return;
  }
  static const uint8_t block60[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block60, 1); }))) {
    return;
  }
  static const uint8_t block61[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block61, 1); }))) {
    return;
  }
  static const uint8_t block62[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block62, 1); }))) {
    return;
  }
  static const uint8_t block63[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block63, 1); }))) {
    return;
  }
  static const uint8_t block64[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block64, 1); }))) {
    return;
  }
  static const uint8_t block65[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block65, 1); }))) {
    return;
  }
  static const uint8_t block66[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block66, 1); }))) {
    return;
  }
  static const uint8_t block67[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block67, 1); }))) {
    return;
  }
  static const uint8_t block68[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block68, 1); }))) {
    return;
  }
  static const uint8_t block69[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block69, 1); }))) {
    return;
  }
  static const uint8_t block70[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block70, 1); }))) {
    return;
  }
  if (!fuzzer.StepTime(18446744073709551612ull)) {
    return;
  }
  static const uint8_t block71[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block71, 1); }))) {
    return;
  }
  static const uint8_t block72[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block72, 1); }))) {
    return;
  }
}

// Exposed a bug with too many outstanding sends.
TEST_P(PacketProtocolTest, _ffaebbd1370c62dee2d0c6f85553d47a88e6c320) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 1); }))) {
    return;
  }
  static const uint8_t block1[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 1); }))) {
    return;
  }
  static const uint8_t block2[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 1); }))) {
    return;
  }
  static const uint8_t block3[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 1); }))) {
    return;
  }
  static const uint8_t block4[] = {
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x28, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x27, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               129, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block4, 129); }))) {
    return;
  }
  static const uint8_t block5[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 1); }))) {
    return;
  }
  static const uint8_t block6[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 1); }))) {
    return;
  }
  static const uint8_t block7[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block7, 1); }))) {
    return;
  }
  static const uint8_t block8[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block8, 1); }))) {
    return;
  }
  static const uint8_t block9[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block9, 1); }))) {
    return;
  }
  static const uint8_t block10[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(5),
                               [](uint8_t* p) { memcpy(p, block10, 1); }))) {
    return;
  }
  static const uint8_t block11[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block11, 1); }))) {
    return;
  }
  static const uint8_t block12[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block12, 1); }))) {
    return;
  }
  static const uint8_t block13[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block13, 1); }))) {
    return;
  }
  static const uint8_t block14[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block14, 1); }))) {
    return;
  }
  static const uint8_t block15[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block15, 1); }))) {
    return;
  }
  static const uint8_t block16[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block16, 1); }))) {
    return;
  }
  static const uint8_t block17[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block17, 1); }))) {
    return;
  }
  static const uint8_t block18[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block18, 1); }))) {
    return;
  }
  static const uint8_t block19[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block19, 1); }))) {
    return;
  }
  static const uint8_t block20[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block20, 1); }))) {
    return;
  }
  static const uint8_t block21[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block21, 1); }))) {
    return;
  }
  static const uint8_t block22[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block22, 1); }))) {
    return;
  }
  static const uint8_t block23[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block23, 1); }))) {
    return;
  }
  static const uint8_t block24[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block24, 1); }))) {
    return;
  }
  static const uint8_t block25[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block25, 1); }))) {
    return;
  }
  static const uint8_t block26[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block26, 1); }))) {
    return;
  }
  static const uint8_t block27[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block27, 1); }))) {
    return;
  }
  static const uint8_t block28[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block28, 1); }))) {
    return;
  }
  static const uint8_t block29[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block29, 1); }))) {
    return;
  }
  static const uint8_t block30[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block30, 1); }))) {
    return;
  }
  static const uint8_t block31[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block31, 1); }))) {
    return;
  }
  static const uint8_t block32[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block32, 1); }))) {
    return;
  }
  static const uint8_t block33[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block33, 1); }))) {
    return;
  }
  static const uint8_t block34[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block34, 1); }))) {
    return;
  }
  static const uint8_t block35[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block35, 1); }))) {
    return;
  }
  static const uint8_t block36[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block36, 1); }))) {
    return;
  }
  static const uint8_t block37[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block37, 1); }))) {
    return;
  }
  static const uint8_t block38[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block38, 1); }))) {
    return;
  }
  static const uint8_t block39[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block39, 1); }))) {
    return;
  }
  static const uint8_t block40[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block40, 1); }))) {
    return;
  }
  static const uint8_t block41[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block41, 1); }))) {
    return;
  }
  static const uint8_t block42[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block42, 1); }))) {
    return;
  }
  static const uint8_t block43[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block43, 1); }))) {
    return;
  }
  static const uint8_t block44[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block44, 1); }))) {
    return;
  }
  static const uint8_t block45[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block45, 1); }))) {
    return;
  }
  static const uint8_t block46[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block46, 1); }))) {
    return;
  }
  static const uint8_t block47[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block47, 1); }))) {
    return;
  }
  static const uint8_t block48[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block48, 1); }))) {
    return;
  }
  static const uint8_t block49[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block49, 1); }))) {
    return;
  }
  static const uint8_t block50[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block50, 1); }))) {
    return;
  }
  static const uint8_t block51[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block51, 1); }))) {
    return;
  }
  static const uint8_t block52[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block52, 1); }))) {
    return;
  }
  static const uint8_t block53[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block53, 1); }))) {
    return;
  }
  static const uint8_t block54[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block54, 1); }))) {
    return;
  }
  static const uint8_t block55[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block55, 1); }))) {
    return;
  }
  static const uint8_t block56[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block56, 1); }))) {
    return;
  }
  static const uint8_t block57[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block57, 1); }))) {
    return;
  }
  static const uint8_t block58[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block58, 1); }))) {
    return;
  }
  static const uint8_t block59[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block59, 1); }))) {
    return;
  }
  static const uint8_t block60[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block60, 1); }))) {
    return;
  }
  static const uint8_t block61[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block61, 1); }))) {
    return;
  }
  static const uint8_t block62[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block62, 1); }))) {
    return;
  }
  static const uint8_t block63[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block63, 1); }))) {
    return;
  }
  static const uint8_t block64[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block64, 1); }))) {
    return;
  }
  static const uint8_t block65[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block65, 1); }))) {
    return;
  }
  static const uint8_t block66[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block66, 1); }))) {
    return;
  }
  static const uint8_t block67[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block67, 1); }))) {
    return;
  }
  static const uint8_t block68[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block68, 1); }))) {
    return;
  }
  static const uint8_t block69[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block69, 1); }))) {
    return;
  }
  static const uint8_t block70[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block70, 1); }))) {
    return;
  }
  static const uint8_t block71[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block71, 1); }))) {
    return;
  }
  static const uint8_t block72[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block72, 1); }))) {
    return;
  }
  static const uint8_t block73[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block73, 1); }))) {
    return;
  }
  static const uint8_t block74[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block74, 1); }))) {
    return;
  }
  static const uint8_t block75[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block75, 1); }))) {
    return;
  }
  static const uint8_t block76[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block76, 1); }))) {
    return;
  }
  static const uint8_t block77[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block77, 1); }))) {
    return;
  }
  static const uint8_t block78[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block78, 1); }))) {
    return;
  }
  static const uint8_t block79[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block79, 1); }))) {
    return;
  }
  static const uint8_t block80[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block80, 1); }))) {
    return;
  }
  static const uint8_t block81[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block81, 1); }))) {
    return;
  }
  static const uint8_t block82[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block82, 1); }))) {
    return;
  }
  static const uint8_t block83[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block83, 1); }))) {
    return;
  }
  static const uint8_t block84[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block84, 1); }))) {
    return;
  }
  static const uint8_t block85[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block85, 1); }))) {
    return;
  }
  static const uint8_t block86[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block86, 1); }))) {
    return;
  }
  static const uint8_t block87[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block87, 1); }))) {
    return;
  }
  static const uint8_t block88[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block88, 1); }))) {
    return;
  }
  static const uint8_t block89[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block89, 1); }))) {
    return;
  }
  static const uint8_t block90[] = {0x3a};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block90, 1); }))) {
    return;
  }
  static const uint8_t block91[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block91, 1); }))) {
    return;
  }
  static const uint8_t block92[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block92, 1); }))) {
    return;
  }
  static const uint8_t block93[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block93, 1); }))) {
    return;
  }
  static const uint8_t block94[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block94, 1); }))) {
    return;
  }
  static const uint8_t block95[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block95, 1); }))) {
    return;
  }
  static const uint8_t block96[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block96, 1); }))) {
    return;
  }
  static const uint8_t block97[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block97, 1); }))) {
    return;
  }
  static const uint8_t block98[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block98, 1); }))) {
    return;
  }
  static const uint8_t block99[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block99, 1); }))) {
    return;
  }
  static const uint8_t block100[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block100, 1); }))) {
    return;
  }
  static const uint8_t block101[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block101, 1); }))) {
    return;
  }
  static const uint8_t block102[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block102, 1); }))) {
    return;
  }
  static const uint8_t block103[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block103, 1); }))) {
    return;
  }
  static const uint8_t block104[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block104, 1); }))) {
    return;
  }
  static const uint8_t block105[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block105, 1); }))) {
    return;
  }
  static const uint8_t block106[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block106, 1); }))) {
    return;
  }
  static const uint8_t block107[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block107, 1); }))) {
    return;
  }
  static const uint8_t block108[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block108, 1); }))) {
    return;
  }
  static const uint8_t block109[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block109, 1); }))) {
    return;
  }
  static const uint8_t block110[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block110, 1); }))) {
    return;
  }
  static const uint8_t block111[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block111, 1); }))) {
    return;
  }
  static const uint8_t block112[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block112, 1); }))) {
    return;
  }
  static const uint8_t block113[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block113, 1); }))) {
    return;
  }
  static const uint8_t block114[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block114, 1); }))) {
    return;
  }
  static const uint8_t block115[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block115, 1); }))) {
    return;
  }
  static const uint8_t block116[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block116, 1); }))) {
    return;
  }
  static const uint8_t block117[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block117, 1); }))) {
    return;
  }
  static const uint8_t block118[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block118, 1); }))) {
    return;
  }
  static const uint8_t block119[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block119, 1); }))) {
    return;
  }
  static const uint8_t block120[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block120, 0); }))) {
    return;
  }
  static const uint8_t block121[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block121, 1); }))) {
    return;
  }
  static const uint8_t block122[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block122, 1); }))) {
    return;
  }
  static const uint8_t block123[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block123, 1); }))) {
    return;
  }
  static const uint8_t block124[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block124, 1); }))) {
    return;
  }
  static const uint8_t block125[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block125, 1); }))) {
    return;
  }
  static const uint8_t block126[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block126, 1); }))) {
    return;
  }
  static const uint8_t block127[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block127, 1); }))) {
    return;
  }
  static const uint8_t block128[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block128, 1); }))) {
    return;
  }
  static const uint8_t block129[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block129, 1); }))) {
    return;
  }
  static const uint8_t block130[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block130, 1); }))) {
    return;
  }
  static const uint8_t block131[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block131, 1); }))) {
    return;
  }
  static const uint8_t block132[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block132, 1); }))) {
    return;
  }
  static const uint8_t block133[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block133, 1); }))) {
    return;
  }
  static const uint8_t block134[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block134, 1); }))) {
    return;
  }
  static const uint8_t block135[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block135, 1); }))) {
    return;
  }
  static const uint8_t block136[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block136, 1); }))) {
    return;
  }
  static const uint8_t block137[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block137, 1); }))) {
    return;
  }
  static const uint8_t block138[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block138, 1); }))) {
    return;
  }
  static const uint8_t block139[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block139, 1); }))) {
    return;
  }
  static const uint8_t block140[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block140, 1); }))) {
    return;
  }
  static const uint8_t block141[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block141, 1); }))) {
    return;
  }
  static const uint8_t block142[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block142, 1); }))) {
    return;
  }
  static const uint8_t block143[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block143, 1); }))) {
    return;
  }
  static const uint8_t block144[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block144, 1); }))) {
    return;
  }
  static const uint8_t block145[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block145, 1); }))) {
    return;
  }
  static const uint8_t block146[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block146, 1); }))) {
    return;
  }
  static const uint8_t block147[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block147, 1); }))) {
    return;
  }
  static const uint8_t block148[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block148, 1); }))) {
    return;
  }
  static const uint8_t block149[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block149, 1); }))) {
    return;
  }
  static const uint8_t block150[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block150, 1); }))) {
    return;
  }
  static const uint8_t block151[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block151, 1); }))) {
    return;
  }
  static const uint8_t block152[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block152, 1); }))) {
    return;
  }
  static const uint8_t block153[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block153, 1); }))) {
    return;
  }
  static const uint8_t block154[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block154, 1); }))) {
    return;
  }
  static const uint8_t block155[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block155, 1); }))) {
    return;
  }
  static const uint8_t block156[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block156, 1); }))) {
    return;
  }
  static const uint8_t block157[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block157, 1); }))) {
    return;
  }
  static const uint8_t block158[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block158, 1); }))) {
    return;
  }
  static const uint8_t block159[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block159, 1); }))) {
    return;
  }
  static const uint8_t block160[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block160, 1); }))) {
    return;
  }
  static const uint8_t block161[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block161, 1); }))) {
    return;
  }
  static const uint8_t block162[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block162, 1); }))) {
    return;
  }
  static const uint8_t block163[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block163, 1); }))) {
    return;
  }
  static const uint8_t block164[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block164, 1); }))) {
    return;
  }
  static const uint8_t block165[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block165, 1); }))) {
    return;
  }
  static const uint8_t block166[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block166, 1); }))) {
    return;
  }
  static const uint8_t block167[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block167, 1); }))) {
    return;
  }
  static const uint8_t block168[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block168, 1); }))) {
    return;
  }
  static const uint8_t block169[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block169, 1); }))) {
    return;
  }
  static const uint8_t block170[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block170, 1); }))) {
    return;
  }
  static const uint8_t block171[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block171, 1); }))) {
    return;
  }
  static const uint8_t block172[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block172, 1); }))) {
    return;
  }
  static const uint8_t block173[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block173, 1); }))) {
    return;
  }
  static const uint8_t block174[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block174, 1); }))) {
    return;
  }
  static const uint8_t block175[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block175, 1); }))) {
    return;
  }
  static const uint8_t block176[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block176, 1); }))) {
    return;
  }
  static const uint8_t block177[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block177, 1); }))) {
    return;
  }
  static const uint8_t block178[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block178, 1); }))) {
    return;
  }
  static const uint8_t block179[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block179, 1); }))) {
    return;
  }
  static const uint8_t block180[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block180, 1); }))) {
    return;
  }
  static const uint8_t block181[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block181, 1); }))) {
    return;
  }
  static const uint8_t block182[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block182, 1); }))) {
    return;
  }
  static const uint8_t block183[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block183, 1); }))) {
    return;
  }
  static const uint8_t block184[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block184, 1); }))) {
    return;
  }
  static const uint8_t block185[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block185, 1); }))) {
    return;
  }
  static const uint8_t block186[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block186, 1); }))) {
    return;
  }
  static const uint8_t block187[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block187, 1); }))) {
    return;
  }
  static const uint8_t block188[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block188, 1); }))) {
    return;
  }
  static const uint8_t block189[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block189, 1); }))) {
    return;
  }
  static const uint8_t block190[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block190, 1); }))) {
    return;
  }
  static const uint8_t block191[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block191, 1); }))) {
    return;
  }
  static const uint8_t block192[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block192, 1); }))) {
    return;
  }
  static const uint8_t block193[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block193, 1); }))) {
    return;
  }
  static const uint8_t block194[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block194, 1); }))) {
    return;
  }
  static const uint8_t block195[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block195, 1); }))) {
    return;
  }
  static const uint8_t block196[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block196, 1); }))) {
    return;
  }
  static const uint8_t block197[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block197, 1); }))) {
    return;
  }
  static const uint8_t block198[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block198, 1); }))) {
    return;
  }
  static const uint8_t block199[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block199, 1); }))) {
    return;
  }
  static const uint8_t block200[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block200, 1); }))) {
    return;
  }
  static const uint8_t block201[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block201, 1); }))) {
    return;
  }
  static const uint8_t block202[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block202, 1); }))) {
    return;
  }
  static const uint8_t block203[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block203, 1); }))) {
    return;
  }
  static const uint8_t block204[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block204, 1); }))) {
    return;
  }
  static const uint8_t block205[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block205, 1); }))) {
    return;
  }
  static const uint8_t block206[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block206, 1); }))) {
    return;
  }
  static const uint8_t block207[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block207, 1); }))) {
    return;
  }
  static const uint8_t block208[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block208, 1); }))) {
    return;
  }
  static const uint8_t block209[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block209, 1); }))) {
    return;
  }
  static const uint8_t block210[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block210, 1); }))) {
    return;
  }
  static const uint8_t block211[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block211, 1); }))) {
    return;
  }
  static const uint8_t block212[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block212, 1); }))) {
    return;
  }
  static const uint8_t block213[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block213, 1); }))) {
    return;
  }
  static const uint8_t block214[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block214, 1); }))) {
    return;
  }
  static const uint8_t block215[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block215, 1); }))) {
    return;
  }
  static const uint8_t block216[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block216, 1); }))) {
    return;
  }
  static const uint8_t block217[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block217, 1); }))) {
    return;
  }
  static const uint8_t block218[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block218, 1); }))) {
    return;
  }
  static const uint8_t block219[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block219, 1); }))) {
    return;
  }
  static const uint8_t block220[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block220, 1); }))) {
    return;
  }
  static const uint8_t block221[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block221, 1); }))) {
    return;
  }
  static const uint8_t block222[] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                     0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                     0x01, 0x01, 0x01, 0x01, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               17, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block222, 17); }))) {
    return;
  }
  static const uint8_t block223[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block223, 1); }))) {
    return;
  }
  static const uint8_t block224[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block224, 1); }))) {
    return;
  }
  static const uint8_t block225[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block225, 1); }))) {
    return;
  }
  static const uint8_t block226[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block226, 1); }))) {
    return;
  }
  static const uint8_t block227[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block227, 1); }))) {
    return;
  }
  static const uint8_t block228[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block228, 1); }))) {
    return;
  }
  static const uint8_t block229[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block229, 1); }))) {
    return;
  }
  static const uint8_t block230[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block230, 1); }))) {
    return;
  }
  static const uint8_t block231[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block231, 1); }))) {
    return;
  }
  static const uint8_t block232[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block232, 1); }))) {
    return;
  }
  static const uint8_t block233[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block233, 1); }))) {
    return;
  }
  static const uint8_t block234[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block234, 1); }))) {
    return;
  }
  static const uint8_t block235[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block235, 1); }))) {
    return;
  }
  static const uint8_t block236[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block236, 1); }))) {
    return;
  }
  static const uint8_t block237[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block237, 1); }))) {
    return;
  }
  static const uint8_t block238[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block238, 1); }))) {
    return;
  }
  static const uint8_t block239[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block239, 1); }))) {
    return;
  }
  static const uint8_t block240[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block240, 1); }))) {
    return;
  }
  static const uint8_t block241[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block241, 1); }))) {
    return;
  }
  static const uint8_t block242[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block242, 1); }))) {
    return;
  }
  static const uint8_t block243[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block243, 1); }))) {
    return;
  }
  static const uint8_t block244[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block244, 1); }))) {
    return;
  }
  static const uint8_t block245[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block245, 1); }))) {
    return;
  }
  static const uint8_t block246[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block246, 1); }))) {
    return;
  }
  static const uint8_t block247[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block247, 1); }))) {
    return;
  }
  static const uint8_t block248[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block248, 1); }))) {
    return;
  }
  static const uint8_t block249[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block249, 1); }))) {
    return;
  }
  static const uint8_t block250[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block250, 1); }))) {
    return;
  }
  static const uint8_t block251[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block251, 1); }))) {
    return;
  }
  static const uint8_t block252[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block252, 1); }))) {
    return;
  }
  static const uint8_t block253[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block253, 1); }))) {
    return;
  }
  static const uint8_t block254[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block254, 1); }))) {
    return;
  }
  static const uint8_t block255[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block255, 1); }))) {
    return;
  }
  static const uint8_t block256[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block256, 1); }))) {
    return;
  }
  static const uint8_t block257[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block257, 1); }))) {
    return;
  }
  static const uint8_t block258[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block258, 1); }))) {
    return;
  }
  static const uint8_t block259[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block259, 1); }))) {
    return;
  }
  static const uint8_t block260[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block260, 1); }))) {
    return;
  }
  static const uint8_t block261[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block261, 1); }))) {
    return;
  }
  static const uint8_t block262[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block262, 1); }))) {
    return;
  }
  static const uint8_t block263[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block263, 1); }))) {
    return;
  }
  static const uint8_t block264[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block264, 0); }))) {
    return;
  }
  static const uint8_t block265[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block265, 1); }))) {
    return;
  }
  static const uint8_t block266[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block266, 1); }))) {
    return;
  }
  static const uint8_t block267[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block267, 1); }))) {
    return;
  }
  static const uint8_t block268[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block268, 1); }))) {
    return;
  }
  static const uint8_t block269[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block269, 1); }))) {
    return;
  }
  static const uint8_t block270[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block270, 1); }))) {
    return;
  }
  static const uint8_t block271[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block271, 1); }))) {
    return;
  }
  static const uint8_t block272[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block272, 1); }))) {
    return;
  }
  static const uint8_t block273[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block273, 1); }))) {
    return;
  }
  static const uint8_t block274[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block274, 1); }))) {
    return;
  }
  static const uint8_t block275[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block275, 1); }))) {
    return;
  }
  static const uint8_t block276[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block276, 1); }))) {
    return;
  }
  static const uint8_t block277[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block277, 1); }))) {
    return;
  }
  static const uint8_t block278[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block278, 1); }))) {
    return;
  }
  static const uint8_t block279[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block279, 1); }))) {
    return;
  }
  static const uint8_t block280[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block280, 1); }))) {
    return;
  }
  static const uint8_t block281[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block281, 1); }))) {
    return;
  }
  static const uint8_t block282[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block282, 1); }))) {
    return;
  }
  static const uint8_t block283[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block283, 1); }))) {
    return;
  }
  static const uint8_t block284[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block284, 1); }))) {
    return;
  }
  static const uint8_t block285[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block285, 1); }))) {
    return;
  }
  static const uint8_t block286[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block286, 1); }))) {
    return;
  }
  static const uint8_t block287[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block287, 1); }))) {
    return;
  }
  static const uint8_t block288[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block288, 1); }))) {
    return;
  }
  static const uint8_t block289[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block289, 1); }))) {
    return;
  }
  static const uint8_t block290[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block290, 1); }))) {
    return;
  }
  static const uint8_t block291[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block291, 1); }))) {
    return;
  }
  static const uint8_t block292[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block292, 1); }))) {
    return;
  }
  static const uint8_t block293[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block293, 1); }))) {
    return;
  }
  static const uint8_t block294[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block294, 1); }))) {
    return;
  }
  static const uint8_t block295[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block295, 1); }))) {
    return;
  }
  static const uint8_t block296[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block296, 1); }))) {
    return;
  }
  static const uint8_t block297[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block297, 1); }))) {
    return;
  }
  static const uint8_t block298[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block298, 1); }))) {
    return;
  }
  static const uint8_t block299[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block299, 1); }))) {
    return;
  }
  static const uint8_t block300[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block300, 1); }))) {
    return;
  }
  static const uint8_t block301[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block301, 1); }))) {
    return;
  }
  static const uint8_t block302[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block302, 1); }))) {
    return;
  }
  static const uint8_t block303[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block303, 1); }))) {
    return;
  }
  static const uint8_t block304[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block304, 1); }))) {
    return;
  }
  static const uint8_t block305[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block305, 1); }))) {
    return;
  }
  static const uint8_t block306[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block306, 1); }))) {
    return;
  }
  static const uint8_t block307[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block307, 1); }))) {
    return;
  }
  static const uint8_t block308[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block308, 1); }))) {
    return;
  }
  static const uint8_t block309[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block309, 1); }))) {
    return;
  }
  static const uint8_t block310[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block310, 1); }))) {
    return;
  }
  static const uint8_t block311[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block311, 1); }))) {
    return;
  }
  static const uint8_t block312[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block312, 1); }))) {
    return;
  }
  static const uint8_t block313[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block313, 1); }))) {
    return;
  }
  static const uint8_t block314[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block314, 1); }))) {
    return;
  }
  static const uint8_t block315[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block315, 1); }))) {
    return;
  }
  static const uint8_t block316[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block316, 1); }))) {
    return;
  }
  static const uint8_t block317[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block317, 1); }))) {
    return;
  }
  static const uint8_t block318[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block318, 1); }))) {
    return;
  }
  static const uint8_t block319[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block319, 1); }))) {
    return;
  }
  static const uint8_t block320[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block320, 1); }))) {
    return;
  }
  static const uint8_t block321[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block321, 1); }))) {
    return;
  }
  static const uint8_t block322[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block322, 1); }))) {
    return;
  }
  static const uint8_t block323[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block323, 1); }))) {
    return;
  }
  static const uint8_t block324[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block324, 1); }))) {
    return;
  }
  static const uint8_t block325[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block325, 1); }))) {
    return;
  }
  static const uint8_t block326[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block326, 1); }))) {
    return;
  }
  static const uint8_t block327[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block327, 1); }))) {
    return;
  }
  static const uint8_t block328[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block328, 1); }))) {
    return;
  }
  static const uint8_t block329[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block329, 1); }))) {
    return;
  }
  static const uint8_t block330[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block330, 1); }))) {
    return;
  }
  static const uint8_t block331[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block331, 1); }))) {
    return;
  }
  static const uint8_t block332[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block332, 1); }))) {
    return;
  }
  static const uint8_t block333[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block333, 1); }))) {
    return;
  }
  static const uint8_t block334[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block334, 1); }))) {
    return;
  }
  static const uint8_t block335[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block335, 1); }))) {
    return;
  }
  static const uint8_t block336[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block336, 1); }))) {
    return;
  }
  static const uint8_t block337[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block337, 1); }))) {
    return;
  }
  static const uint8_t block338[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block338, 1); }))) {
    return;
  }
  static const uint8_t block339[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block339, 1); }))) {
    return;
  }
  static const uint8_t block340[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block340, 1); }))) {
    return;
  }
  static const uint8_t block341[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block341, 1); }))) {
    return;
  }
  static const uint8_t block342[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block342, 1); }))) {
    return;
  }
  static const uint8_t block343[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block343, 1); }))) {
    return;
  }
  static const uint8_t block344[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block344, 1); }))) {
    return;
  }
  static const uint8_t block345[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block345, 1); }))) {
    return;
  }
  static const uint8_t block346[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block346, 1); }))) {
    return;
  }
  static const uint8_t block347[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block347, 1); }))) {
    return;
  }
  static const uint8_t block348[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block348, 1); }))) {
    return;
  }
  static const uint8_t block349[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block349, 1); }))) {
    return;
  }
  static const uint8_t block350[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block350, 1); }))) {
    return;
  }
  static const uint8_t block351[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block351, 1); }))) {
    return;
  }
  static const uint8_t block352[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block352, 1); }))) {
    return;
  }
  static const uint8_t block353[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block353, 1); }))) {
    return;
  }
  static const uint8_t block354[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block354, 1); }))) {
    return;
  }
  static const uint8_t block355[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block355, 1); }))) {
    return;
  }
  static const uint8_t block356[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block356, 1); }))) {
    return;
  }
  static const uint8_t block357[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block357, 1); }))) {
    return;
  }
  static const uint8_t block358[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block358, 1); }))) {
    return;
  }
  static const uint8_t block359[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block359, 1); }))) {
    return;
  }
  static const uint8_t block360[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block360, 1); }))) {
    return;
  }
  static const uint8_t block361[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block361, 1); }))) {
    return;
  }
  static const uint8_t block362[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block362, 1); }))) {
    return;
  }
  static const uint8_t block363[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block363, 1); }))) {
    return;
  }
  static const uint8_t block364[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block364, 1); }))) {
    return;
  }
  static const uint8_t block365[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block365, 1); }))) {
    return;
  }
  static const uint8_t block366[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block366, 1); }))) {
    return;
  }
  static const uint8_t block367[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block367, 1); }))) {
    return;
  }
  static const uint8_t block368[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block368, 1); }))) {
    return;
  }
  static const uint8_t block369[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block369, 1); }))) {
    return;
  }
  static const uint8_t block370[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block370, 1); }))) {
    return;
  }
  static const uint8_t block371[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block371, 1); }))) {
    return;
  }
  static const uint8_t block372[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block372, 1); }))) {
    return;
  }
  static const uint8_t block373[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block373, 1); }))) {
    return;
  }
  static const uint8_t block374[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block374, 1); }))) {
    return;
  }
  static const uint8_t block375[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block375, 1); }))) {
    return;
  }
  static const uint8_t block376[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block376, 1); }))) {
    return;
  }
  static const uint8_t block377[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block377, 1); }))) {
    return;
  }
  static const uint8_t block378[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block378, 1); }))) {
    return;
  }
  static const uint8_t block379[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block379, 1); }))) {
    return;
  }
  static const uint8_t block380[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block380, 1); }))) {
    return;
  }
  static const uint8_t block381[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block381, 1); }))) {
    return;
  }
  static const uint8_t block382[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block382, 1); }))) {
    return;
  }
  static const uint8_t block383[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block383, 1); }))) {
    return;
  }
  static const uint8_t block384[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block384, 1); }))) {
    return;
  }
  static const uint8_t block385[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block385, 1); }))) {
    return;
  }
  static const uint8_t block386[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block386, 1); }))) {
    return;
  }
  static const uint8_t block387[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block387, 1); }))) {
    return;
  }
  static const uint8_t block388[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block388, 1); }))) {
    return;
  }
  static const uint8_t block389[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block389, 1); }))) {
    return;
  }
  static const uint8_t block390[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block390, 1); }))) {
    return;
  }
  static const uint8_t block391[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block391, 1); }))) {
    return;
  }
  static const uint8_t block392[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block392, 1); }))) {
    return;
  }
  static const uint8_t block393[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block393, 1); }))) {
    return;
  }
  static const uint8_t block394[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block394, 1); }))) {
    return;
  }
  static const uint8_t block395[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block395, 1); }))) {
    return;
  }
  static const uint8_t block396[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block396, 1); }))) {
    return;
  }
  static const uint8_t block397[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block397, 1); }))) {
    return;
  }
  static const uint8_t block398[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block398, 1); }))) {
    return;
  }
  static const uint8_t block399[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block399, 1); }))) {
    return;
  }
  static const uint8_t block400[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block400, 1); }))) {
    return;
  }
  static const uint8_t block401[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block401, 1); }))) {
    return;
  }
  static const uint8_t block402[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block402, 1); }))) {
    return;
  }
  static const uint8_t block403[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block403, 1); }))) {
    return;
  }
  static const uint8_t block404[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block404, 1); }))) {
    return;
  }
  static const uint8_t block405[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block405, 1); }))) {
    return;
  }
  static const uint8_t block406[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block406, 1); }))) {
    return;
  }
  static const uint8_t block407[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block407, 1); }))) {
    return;
  }
  static const uint8_t block408[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block408, 1); }))) {
    return;
  }
  static const uint8_t block409[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block409, 1); }))) {
    return;
  }
  static const uint8_t block410[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block410, 1); }))) {
    return;
  }
  static const uint8_t block411[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block411, 1); }))) {
    return;
  }
  static const uint8_t block412[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block412, 1); }))) {
    return;
  }
  static const uint8_t block413[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block413, 1); }))) {
    return;
  }
  static const uint8_t block414[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block414, 1); }))) {
    return;
  }
  static const uint8_t block415[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block415, 1); }))) {
    return;
  }
  static const uint8_t block416[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block416, 1); }))) {
    return;
  }
  static const uint8_t block417[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block417, 1); }))) {
    return;
  }
  static const uint8_t block418[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block418, 1); }))) {
    return;
  }
  static const uint8_t block419[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block419, 1); }))) {
    return;
  }
  static const uint8_t block420[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block420, 1); }))) {
    return;
  }
  static const uint8_t block421[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block421, 1); }))) {
    return;
  }
  static const uint8_t block422[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block422, 1); }))) {
    return;
  }
  static const uint8_t block423[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block423, 1); }))) {
    return;
  }
  static const uint8_t block424[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block424, 1); }))) {
    return;
  }
  static const uint8_t block425[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block425, 1); }))) {
    return;
  }
  static const uint8_t block426[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block426, 1); }))) {
    return;
  }
  static const uint8_t block427[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block427, 1); }))) {
    return;
  }
  static const uint8_t block428[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block428, 1); }))) {
    return;
  }
  static const uint8_t block429[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block429, 1); }))) {
    return;
  }
  static const uint8_t block430[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block430, 1); }))) {
    return;
  }
  static const uint8_t block431[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block431, 1); }))) {
    return;
  }
  static const uint8_t block432[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block432, 1); }))) {
    return;
  }
  static const uint8_t block433[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block433, 1); }))) {
    return;
  }
  static const uint8_t block434[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block434, 1); }))) {
    return;
  }
  static const uint8_t block435[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block435, 1); }))) {
    return;
  }
  static const uint8_t block436[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block436, 1); }))) {
    return;
  }
  static const uint8_t block437[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block437, 1); }))) {
    return;
  }
  static const uint8_t block438[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block438, 1); }))) {
    return;
  }
  static const uint8_t block439[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block439, 1); }))) {
    return;
  }
  static const uint8_t block440[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block440, 1); }))) {
    return;
  }
  static const uint8_t block441[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block441, 1); }))) {
    return;
  }
  static const uint8_t block442[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block442, 1); }))) {
    return;
  }
  static const uint8_t block443[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block443, 1); }))) {
    return;
  }
  static const uint8_t block444[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block444, 1); }))) {
    return;
  }
  static const uint8_t block445[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block445, 1); }))) {
    return;
  }
  static const uint8_t block446[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block446, 1); }))) {
    return;
  }
  static const uint8_t block447[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block447, 1); }))) {
    return;
  }
  static const uint8_t block448[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block448, 1); }))) {
    return;
  }
  static const uint8_t block449[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block449, 1); }))) {
    return;
  }
  static const uint8_t block450[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block450, 1); }))) {
    return;
  }
  static const uint8_t block451[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block451, 1); }))) {
    return;
  }
  static const uint8_t block452[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block452, 1); }))) {
    return;
  }
  static const uint8_t block453[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block453, 1); }))) {
    return;
  }
  static const uint8_t block454[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block454, 1); }))) {
    return;
  }
  static const uint8_t block455[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block455, 1); }))) {
    return;
  }
  static const uint8_t block456[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block456, 1); }))) {
    return;
  }
  static const uint8_t block457[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block457, 1); }))) {
    return;
  }
  static const uint8_t block458[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block458, 1); }))) {
    return;
  }
  static const uint8_t block459[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block459, 1); }))) {
    return;
  }
  static const uint8_t block460[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block460, 1); }))) {
    return;
  }
  static const uint8_t block461[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block461, 1); }))) {
    return;
  }
  static const uint8_t block462[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block462, 1); }))) {
    return;
  }
  static const uint8_t block463[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block463, 1); }))) {
    return;
  }
  static const uint8_t block464[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block464, 1); }))) {
    return;
  }
  static const uint8_t block465[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block465, 1); }))) {
    return;
  }
  static const uint8_t block466[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block466, 1); }))) {
    return;
  }
  static const uint8_t block467[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block467, 1); }))) {
    return;
  }
  static const uint8_t block468[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block468, 1); }))) {
    return;
  }
  static const uint8_t block469[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block469, 1); }))) {
    return;
  }
  static const uint8_t block470[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block470, 1); }))) {
    return;
  }
  static const uint8_t block471[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block471, 1); }))) {
    return;
  }
  static const uint8_t block472[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block472, 1); }))) {
    return;
  }
  static const uint8_t block473[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block473, 0); }))) {
    return;
  }
  static const uint8_t block474[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block474, 1); }))) {
    return;
  }
  static const uint8_t block475[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block475, 1); }))) {
    return;
  }
  static const uint8_t block476[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block476, 1); }))) {
    return;
  }
  static const uint8_t block477[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block477, 1); }))) {
    return;
  }
  static const uint8_t block478[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block478, 1); }))) {
    return;
  }
  static const uint8_t block479[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block479, 1); }))) {
    return;
  }
  static const uint8_t block480[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block480, 1); }))) {
    return;
  }
  static const uint8_t block481[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block481, 1); }))) {
    return;
  }
  static const uint8_t block482[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block482, 1); }))) {
    return;
  }
  static const uint8_t block483[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block483, 1); }))) {
    return;
  }
  static const uint8_t block484[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block484, 1); }))) {
    return;
  }
  static const uint8_t block485[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block485, 1); }))) {
    return;
  }
  static const uint8_t block486[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block486, 1); }))) {
    return;
  }
  static const uint8_t block487[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block487, 1); }))) {
    return;
  }
  static const uint8_t block488[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block488, 1); }))) {
    return;
  }
  static const uint8_t block489[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block489, 1); }))) {
    return;
  }
  static const uint8_t block490[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block490, 1); }))) {
    return;
  }
  static const uint8_t block491[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block491, 1); }))) {
    return;
  }
  static const uint8_t block492[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block492, 1); }))) {
    return;
  }
  static const uint8_t block493[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block493, 1); }))) {
    return;
  }
  static const uint8_t block494[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block494, 1); }))) {
    return;
  }
  static const uint8_t block495[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block495, 1); }))) {
    return;
  }
  static const uint8_t block496[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block496, 1); }))) {
    return;
  }
  static const uint8_t block497[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block497, 1); }))) {
    return;
  }
  static const uint8_t block498[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block498, 1); }))) {
    return;
  }
  static const uint8_t block499[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block499, 1); }))) {
    return;
  }
  static const uint8_t block500[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block500, 1); }))) {
    return;
  }
  static const uint8_t block501[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block501, 1); }))) {
    return;
  }
  static const uint8_t block502[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block502, 1); }))) {
    return;
  }
  static const uint8_t block503[] = {0x28};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block503, 1); }))) {
    return;
  }
  static const uint8_t block504[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block504, 1); }))) {
    return;
  }
  static const uint8_t block505[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block505, 1); }))) {
    return;
  }
  static const uint8_t block506[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block506, 1); }))) {
    return;
  }
  static const uint8_t block507[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block507, 1); }))) {
    return;
  }
  static const uint8_t block508[] = {0x01, 0x02};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block508, 2); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  // Snipped... irrelevant to reproduction.
}

// Found a bug in the fuzzer around nack handling.
TEST_P(PacketProtocolTest, _9bfa77589cb379397dafc6661fee887af34c03de) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 1); }))) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(2),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {0x00};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 1); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _cd8ef3987ba3f1dd092946a8e7047ad4552d0fac) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x01, 0xe9};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block0, 2); }))) {
    return;
  }
  if (!fuzzer.StepTime(8388607ull)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _0c1d445350ec75aadc7a14929adc41e65ebba2f6) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(4),
                               [](uint8_t* p) { memcpy(p, block0, 1); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 1)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _868e9a3a3d4cea4addc390fff43898840efd7507) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {0x0b};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block0, 1); }))) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(3),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(3),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  static const uint8_t block3[] = {0xb2};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block3, 1); }))) {
    return;
  }
  if (!fuzzer.StepTime(9223372036854775807ull)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _85d2d9a48ade0bf3055dd43b6b5ecbb468346c07) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.StepTime(0ull)) {
    return;
  }
  static const uint8_t block1[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block1, 1); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(2),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 222)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _12fa5c35c19ac2ff4b0f31b24c18d62458579194) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(42),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.StepTime(0ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block1[] = {0x00, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(247),
                               [](uint8_t* p) { memcpy(p, block1, 2); }))) {
    return;
  }
  static const uint8_t block2[] = {0x00};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(2),
                               [](uint8_t* p) { memcpy(p, block2, 1); }))) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {
      0x9c, 0x3d, 0xff, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00};
  if (!fuzzer.BeginSend(3, Slice::WithInitializerAndBorders(
                               193, Border::Prefix(156),
                               [](uint8_t* p) { memcpy(p, block4, 193); }))) {
    return;
  }
}

TEST_P(PacketProtocolTest, _193597a52cff3e80bc937c4e9635ca5724d2ba17) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.StepTime(0ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block1[] = {0x02, 0x01};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block1, 2); }))) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _3a406a2356275258a2c479209b9aa2af30d4f6ac) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.StepTime(0ull)) {
    return;
  }
  static const uint8_t block1[] = {0x01, 0x02};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(3),
                               [](uint8_t* p) { memcpy(p, block1, 2); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {0x00, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(247),
                               [](uint8_t* p) { memcpy(p, block2, 2); }))) {
    return;
  }
  static const uint8_t block3[] = {0x02, 0x02, 0x64};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               3, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 3); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _58c60d38ea46f0dc5f7eb88d1a0c81264bafd6f6) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.StepTime(0ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block1[] = {0x08, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(247),
                               [](uint8_t* p) { memcpy(p, block1, 2); }))) {
    return;
  }
  static const uint8_t block2[] = {0x02, 0x02, 0x64};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               3, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 3); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _5263d304763e36809560f5f03fb2b64ce3290d23) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.StepTime(9223372036854775807ull)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(255),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(255),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _9dd4fd20942043cf94a266a540cad36aad4bd189) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.StepTime(0ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block1[] = {0x00, 0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(247),
                               [](uint8_t* p) { memcpy(p, block1, 2); }))) {
    return;
  }
  static const uint8_t block2[] = {0x02, 0x02, 0x64};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               3, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 3); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _1840c624d144d5ae1888b38d0d124981da9bfc3b) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(69642597ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 8)) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _29fbbe04441869d515efa7eb1fc5131055d27c1e) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(69642597ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(4282ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 64)) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _493f492251d242ff611a272b088ce943f5b4b061) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(69642597ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 128)) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _46855031ec6fd0b8ca2e6d7a291a510e8c4a0d23) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(69642597ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(960869ull)) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _5266b27f820a72a53d7f46cb40653677bb9c7987) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 3)) {
    return;
  }
  if (!fuzzer.StepTime(1099476662921573ull)) {
    return;
  }
  static const uint8_t block4[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 0); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block6[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block6, 1); }))) {
    return;
  }
  static const uint8_t block7[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block7, 0); }))) {
    return;
  }
}

TEST_P(PacketProtocolTest, _5758e2bf55e70a6f49d3d8b4b603acd897866283) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 3)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block6[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block6, 1); }))) {
    return;
  }
  static const uint8_t block7[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block7, 0); }))) {
    return;
  }
}

TEST_P(PacketProtocolTest, _6d6a08cd1d3617f7fa8a43f00b3f465566430461) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 74)) {
    return;
  }
  if (!fuzzer.StepTime(70164837ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block7[] = {0x00, 0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               2, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block7, 2); }))) {
    return;
  }
}

TEST_P(PacketProtocolTest, _6fccc5426a7b80d3a8cd3919680cb98175367857) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(36),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(551269ull)) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(186),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 1)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _65daa8c3754fcbb66567d583378341f142d3fa84) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(69642597ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 2)) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _616e48475addb69c08c6a3b570fbbd3ffb890c68) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(69642597ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block4[] = {0x00};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 1); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 16)) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
}

TEST_P(PacketProtocolTest, _7e09301e2b1644abb1cbc0d7433aa8a8452288e2) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 3)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  static const uint8_t block4[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block4, 0); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  static const uint8_t block6[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block6, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block7[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block7, 1); }))) {
    return;
  }
  static const uint8_t block8[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block8, 0); }))) {
    return;
  }
}

TEST_P(PacketProtocolTest, _9a9d0c4f2766121be0657c11a5fa3d354b7cf63e) {
  PacketProtocolFuzzer fuzzer(GetParam(), FLAGS_verbose);
  static const uint8_t block0[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block0, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block1[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block1, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 23)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block2[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block2, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  if (!fuzzer.CompleteSend(2, 0)) {
    return;
  }
  static const uint8_t block3[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block3, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 3)) {
    return;
  }
  if (!fuzzer.StepTime(3058021ull)) {
    return;
  }
  static const uint8_t block4[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block4, 0); }))) {
    return;
  }
  static const uint8_t block5[] = {};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(1),
                               [](uint8_t* p) { memcpy(p, block5, 0); }))) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  if (!fuzzer.StepTime(70166885ull)) {
    return;
  }
  if (!fuzzer.CompleteSend(1, 0)) {
    return;
  }
  static const uint8_t block6[] = {0x01};
  if (!fuzzer.BeginSend(1, Slice::WithInitializerAndBorders(
                               1, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block6, 1); }))) {
    return;
  }
  static const uint8_t block7[] = {};
  if (!fuzzer.BeginSend(2, Slice::WithInitializerAndBorders(
                               0, Border::Prefix(0),
                               [](uint8_t* p) { memcpy(p, block7, 0); }))) {
    return;
  }
}

INSTANTIATE_TEST_SUITE_P(PacketProtocol, PacketProtocolTest,
                         ::testing::Values(PacketProtocol::NullCodec(),
                                           &dummy_codec,
                                           aead_codec_chacha.get(),
                                           aead_codec_hmac.get()));

}  // namespace packet_protocol_test
}  // namespace overnet
