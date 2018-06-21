// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/frame.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/rfcomm.h"
#include "gtest/gtest.h"

namespace btlib {
namespace rfcomm {
namespace {

// Contruction of "empty" RFCOMM frame:
// Please see GSM 5.2.1 and RFCOMM 5.1.
// Our frame will have the following characteristics:
//  - Sent from the RFCOMM initiator
//  - SABM frame
//  - Sent to DLCI 0x02
//  - Command frame
//  - P/F bit = 1
constexpr Role kEmptyFrameRole = Role::kInitiator;
constexpr CommandResponse kEmptyFrameCR = CommandResponse::kCommand;
constexpr FrameType kEmptyFrameType = FrameType::kSetAsynchronousBalancedMode;
constexpr DLCI kEmptyFrameDLCI = 0x02;
constexpr bool kEmptyFramePF = true;
constexpr bool kEmptyFrameCreditBasedFlow = false;
const auto kEmptyFrame = common::CreateStaticByteBuffer(
    // Address octet:
    // E/A bit is always 1. C/R bit is 1 in the case of a command being sent
    // from the initiator role. DLCI is 0x02. Thus: 1 (E/A) ++ 1 (C/R) ++ 010000
    // (DLCI) = 11010000
    0b00001011,
    // Control octet:
    // SABM is 1111p100, P/F bit (p) is 1 --> 11111100
    0b00111111,
    // Length octet:
    // Length is 0; E/A bit is thus 1 --> 10000000
    0b00000001,
    // FCS octet:
    // Please see GSM 5.2.1.6, GSM Annex B, and RFCOMM 5.1.1.
    0b01011001);

// Contruction of "helloworld" RFCOMM frame:
//  - Sent from the RFCOMM responder
//  - UIH frame
//  - Sent to DLCI 0x23
//  - Command frame
//  - P/F bit = 0
constexpr Role kHelloFrameRole = Role::kResponder;
constexpr CommandResponse kHelloFrameCR = CommandResponse::kCommand;
constexpr FrameType kHelloFrameType = FrameType::kUnnumberedInfoHeaderCheck;
constexpr DLCI kHelloFrameDLCI = 0x23;
constexpr bool kHelloFramePF = false;
constexpr bool kHelloFrameCreditBasedFlow = false;
const auto kHelloFrameInformation = common::CreateStaticByteBuffer(
    'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd');
const auto kHelloFrame = common::CreateStaticByteBuffer(
    // Address octet:
    // E/A bit is always 1. C/R bit is 0 in the case of a command being sent
    // from the responder role. DLCI is 0x23. Thus: 1 (E/A) ++ 0 (C/R) ++ 110001
    // (DLCI) = 10110001
    0b10001101,
    // Control octet:
    // UIH is 1111p111, P/F bit (p) is 0 --> 11110111
    0b11101111,
    // Length octet:
    // Length is 10; E/A bit is thus 1 --> 10101000
    0b00010101,
    // Information
    'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd',
    // FCS octet:
    // Please see GSM 5.2.1.6, GSM Annex B, and RFCOMM 5.1.1.
    0b10011101);

// Contruction of "hellofuchsia" RFCOMM frame:
//  - Sent from the RFCOMM responder
//  - UIH frame
//  - Sent to DLCI 0x23
//  - Command frame
//  - P/F bit = 1 (credit-based flow on)
constexpr Role kFuchsiaFrameRole = Role::kResponder;
constexpr CommandResponse kFuchsiaFrameCR = CommandResponse::kCommand;
constexpr FrameType kFuchsiaFrameType = FrameType::kUnnumberedInfoHeaderCheck;
constexpr DLCI kFuchsiaFrameDLCI = 0x23;
constexpr bool kFuchsiaFramePF = true;
constexpr bool kFuchsiaFrameCreditBasedFlow = true;
constexpr uint8_t kFuchsiaFrameCredits = 5;
const auto kFuchsiaFrameInformation = common::CreateStaticByteBuffer(
    'h', 'e', 'l', 'l', 'o', 'f', 'u', 'c', 'h', 's', 'i', 'a');
const auto kFuchsiaFrame = common::CreateStaticByteBuffer(
    // Address octet:
    // E/A bit is always 1. C/R bit is 0 in the case of a command being sent
    // from the responder role. DLCI is 0x23. Thus: 1 (E/A) ++ 0 (C/R) ++ 110001
    // (DLCI) = 10110001
    0b10001101,
    // Control octet:
    // UIH is 1111p111, P/F bit (p) is 1 --> 11111111
    0b11111111,
    // Length octet:
    // Length is 12; E/A bit is thus 1 --> 10011000
    0b00011001,
    // Credit octet:
    // Credits = 5
    0b00000101,
    // Information
    'h', 'e', 'l', 'l', 'o', 'f', 'u', 'c', 'h', 's', 'i', 'a',
    // FCS octet:
    // Please see GSM 5.2.1.6, GSM Annex B, and RFCOMM 5.1.1.
    0b10000001);

const auto kInvalidLengthFrame = common::CreateStaticByteBuffer(0, 1, 2);

// Same as the hellofuchsia frame, but information field is too short.
const auto kInvalidLengthFrame2 = common::CreateStaticByteBuffer(
    0b10001101,
    0b11111111,
    0b00011001,
    0b00000101,
    'h', 'e', 'l', 'l', 'o');

// Same as the hellofuchsia frame, but with an invalid FCS.
const auto kInvalidFCSFrame = common::CreateStaticByteBuffer(
    0b10001101,
    0b11111111,
    0b00011001,
    0b00000101,
    'h', 'e', 'l', 'l', 'o', 'f', 'u', 'c', 'h', 's', 'i', 'a',
    0b10000001 + 1);

using RFCOMM_FrameTest = ::testing::Test;

TEST_F(RFCOMM_FrameTest, WriteFrame) {
  SetAsynchronousBalancedModeCommand frame(kEmptyFrameRole, kEmptyFrameDLCI);
  common::DynamicByteBuffer buffer(frame.written_size());
  frame.Write(buffer.mutable_view());
  EXPECT_EQ(4ul, frame.written_size());
  EXPECT_EQ(kEmptyFrame, buffer);
}

TEST_F(RFCOMM_FrameTest, WriteFrameWithData) {
  auto information = common::NewSlabBuffer(kHelloFrameInformation.size());
  kHelloFrameInformation.Copy(information.get());
  UserDataFrame frame(kHelloFrameRole, kHelloFrameCreditBasedFlow,
                      kHelloFrameDLCI, std::move(information));
  common::DynamicByteBuffer buffer(frame.written_size());
  frame.Write(buffer.mutable_view());
  EXPECT_EQ(14ul, frame.written_size());
  EXPECT_EQ(kHelloFrame, buffer);
}

TEST_F(RFCOMM_FrameTest, WriteFrameWithDataAndCredits) {
  auto information = common::NewSlabBuffer(kFuchsiaFrameInformation.size());
  kFuchsiaFrameInformation.Copy(information.get());
  UserDataFrame frame(kFuchsiaFrameRole, kFuchsiaFrameCreditBasedFlow,
                      kFuchsiaFrameDLCI, std::move(information));
  frame.set_credits(kFuchsiaFrameCredits);
  common::DynamicByteBuffer buffer(frame.written_size());
  frame.Write(buffer.mutable_view());
  EXPECT_EQ(17ul, frame.written_size());
  EXPECT_EQ(kFuchsiaFrame, buffer);
}

TEST_F(RFCOMM_FrameTest, ReadFrame) {
  auto frame =
      Frame::Parse(kEmptyFrameCreditBasedFlow, kEmptyFrameRole, kEmptyFrame);
  EXPECT_EQ(kEmptyFrameCR, frame->command_response());
  EXPECT_EQ(kEmptyFrameDLCI, frame->dlci());
  EXPECT_EQ((uint8_t)kEmptyFrameType, frame->control());
  EXPECT_EQ(kEmptyFramePF, frame->poll_final());
  EXPECT_EQ(0, frame->length());
}

TEST_F(RFCOMM_FrameTest, ReadFrameWithData) {
  auto frame =
      Frame::Parse(kHelloFrameCreditBasedFlow, kHelloFrameRole, kHelloFrame);
  EXPECT_EQ((uint8_t)kHelloFrameType, frame->control());
  auto user_data_frame = Frame::ToUserDataFrame(&frame);
  EXPECT_EQ(nullptr, frame);
  EXPECT_EQ(kHelloFrameCR, user_data_frame->command_response());
  EXPECT_EQ(kHelloFrameDLCI, user_data_frame->dlci());
  EXPECT_EQ(kHelloFramePF, user_data_frame->poll_final());
  EXPECT_EQ(kHelloFrameInformation.size(), user_data_frame->length());
  EXPECT_EQ(0, user_data_frame->credits());
  EXPECT_EQ(kHelloFrameInformation, *user_data_frame->TakeInformation());
  EXPECT_EQ(nullptr, user_data_frame->TakeInformation());
}

TEST_F(RFCOMM_FrameTest, ReadFrameWithDataAndCredits) {
  auto frame = Frame::Parse(kFuchsiaFrameCreditBasedFlow, kFuchsiaFrameRole,
                            kFuchsiaFrame);
  EXPECT_EQ((uint8_t)kFuchsiaFrameType, frame->control());
  auto user_data_frame = Frame::ToUserDataFrame(&frame);
  EXPECT_EQ(nullptr, frame);
  EXPECT_EQ(kFuchsiaFrameCR, user_data_frame->command_response());
  EXPECT_EQ(kFuchsiaFrameDLCI, user_data_frame->dlci());
  EXPECT_EQ(kFuchsiaFramePF, user_data_frame->poll_final());
  EXPECT_EQ(kFuchsiaFrameInformation.size(), user_data_frame->length());
  EXPECT_EQ(kFuchsiaFrameCredits, user_data_frame->credits());
  EXPECT_EQ(kFuchsiaFrameInformation, *user_data_frame->TakeInformation());
  EXPECT_EQ(nullptr, user_data_frame->TakeInformation());
}

TEST_F(RFCOMM_FrameTest, ReadInvalidFrame_TooShort) {
  auto frame = Frame::Parse(true, Role::kInitiator, kInvalidLengthFrame);
  EXPECT_EQ(nullptr, frame);
}

TEST_F(RFCOMM_FrameTest, ReadInvalidFrame_EndsUnexpectedly) {
  auto frame = Frame::Parse(true, Role::kInitiator, kInvalidLengthFrame2);
  EXPECT_EQ(nullptr, frame);
}

TEST_F(RFCOMM_FrameTest, ReadInvalidFrame_InvalidFCS) {
  auto frame = Frame::Parse(true, Role::kInitiator, kInvalidFCSFrame);
  EXPECT_EQ(nullptr, frame);
}

}  // namespace
}  // namespace rfcomm
}  // namespace btlib
