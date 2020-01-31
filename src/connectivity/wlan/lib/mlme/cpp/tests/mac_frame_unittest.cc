// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/hw/wlan/wlaninfo.h>
#include <gtest/gtest.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/wlan.h>

#include "test_data.h"
#include "test_utils.h"

namespace wlan {
namespace {

constexpr size_t k4BytePaddingLen = 4;

struct TestHdr1 {
  uint8_t a;
  uint16_t b;
  uint8_t c;
  uint8_t d;

  constexpr size_t len() const { return sizeof(*this); }
} __PACKED;

// Dynamic length based on value of field `has_padding`.
struct TestHdr2 {
  bool has_padding = false;
  uint8_t b;
  uint8_t c;

  size_t len() const { return sizeof(*this) + (has_padding ? k4BytePaddingLen : 0); }
} __PACKED;

struct TestHdr3 {
  uint16_t a;
  uint16_t b;

  constexpr size_t len() const { return sizeof(*this); }
} __PACKED;

struct FixedSizedPayload {
  uint8_t data[10];

  constexpr size_t len() const { return sizeof(*this); }
};

// Frame which holds 3 headers and some optional padding and payload.
template <size_t padding_len, size_t payload_len>
struct TripleHdrFrame {
  TestHdr1 hdr1;
  TestHdr2 hdr2;
  uint8_t padding[padding_len];
  TestHdr3 hdr3;
  uint8_t payload[payload_len];

  static constexpr size_t second_frame_len() {
    return sizeof(TestHdr2) + padding_len + third_frame_len();
  }
  static constexpr size_t second_frame_body_len() { return third_frame_len(); }
  static constexpr size_t third_frame_len() { return sizeof(TestHdr3) + payload_len; }
  static constexpr size_t third_frame_body_len() { return payload_len; }
  static constexpr size_t len() { return sizeof(TripleHdrFrame); }
  static constexpr size_t body_len() { return second_frame_len(); }

} __PACKED;

static std::unique_ptr<Packet> GetPacket(size_t len) {
  auto buffer = GetBuffer(len);
  memset(buffer->data(), 0, len);
  return std::make_unique<Packet>(std::move(buffer), len);
}

using DefaultTripleHdrFrame = TripleHdrFrame<0, 10>;
using PaddedTripleHdrFrame = TripleHdrFrame<4, 10>;

TEST(Frame, General) {
  // Construct initial frame
  auto pkt = GetPacket(DefaultTripleHdrFrame::len());
  auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
  test_frame->hdr1.a = 42;
  test_frame->hdr2.b = 24;

  // Verify frame's accessors and length.
  Frame<TestHdr1> frame{std::move(pkt)};
  ASSERT_FALSE(frame.IsEmpty());
  ASSERT_EQ(frame.len(), DefaultTripleHdrFrame::len());
  ASSERT_EQ(frame.hdr()->a, 42);
  ASSERT_EQ(frame.body_len(), DefaultTripleHdrFrame::body_len());
  ASSERT_EQ(frame.body_data()[1], 24);
}

TEST(Frame, General_Const_Frame) {
  // Construct initial frame.
  auto pkt = GetPacket(DefaultTripleHdrFrame::len());
  auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
  test_frame->hdr1.a = 42;
  test_frame->hdr2.b = 24;

  // Verify a constant frame's accessors and length. Constant accessors differ
  // from regular ones.
  const Frame<TestHdr1> frame{std::move(pkt)};
  ASSERT_FALSE(frame.IsEmpty());
  ASSERT_EQ(frame.len(), DefaultTripleHdrFrame::len());
  ASSERT_EQ(frame.hdr()->a, 42);
  ASSERT_EQ(frame.body_len(), DefaultTripleHdrFrame::body_len());
  ASSERT_EQ(frame.body_data()[1], 24);
}

TEST(Frame, Take) {
  // Construct initial frame.
  auto pkt = GetPacket(DefaultTripleHdrFrame::len());
  auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
  test_frame->hdr1.a = 42;
  test_frame->hdr2.b = 24;

  // Derive frame with unknown body...
  Frame<TestHdr1> frame(std::move(pkt));
  // ... and take the frame's underlying Packet to construct a new, specialized
  // frame.
  Frame<TestHdr1, TestHdr2> specialized_frame(frame.Take());
  // Verify the first frame is considered "taken" and that the new specialized
  // one is valid.
  ASSERT_TRUE(frame.IsEmpty());
  ASSERT_FALSE(specialized_frame.IsEmpty());
  ASSERT_EQ(specialized_frame.len(), DefaultTripleHdrFrame::len());
  ASSERT_EQ(specialized_frame.hdr()->a, 42);
  ASSERT_EQ(specialized_frame.body_len(), DefaultTripleHdrFrame::body_len());
  ASSERT_EQ(specialized_frame.body()->b, 24);
}

TEST(Frame, ExactlySizedBuffer_HdrOnly) {
  // Construct initial frame which has just enough space to hold a header.
  size_t pkt_len = sizeof(TestHdr1);
  auto pkt = GetPacket(pkt_len);

  Frame<TestHdr1> frame(std::move(pkt));
  ASSERT_EQ(frame.len(), sizeof(TestHdr1));
  ASSERT_EQ(frame.body_len(), static_cast<size_t>(0));
}

TEST(Frame, ExactlySizedBuffer_Frame) {
  // Construct initial frame which has just enough space to hold a header and a
  // body.
  size_t pkt_len = sizeof(TestHdr1) + sizeof(FixedSizedPayload);
  auto pkt = GetPacket(pkt_len);

  Frame<TestHdr1, FixedSizedPayload> frame(std::move(pkt));
  ASSERT_EQ(frame.len(), sizeof(TestHdr1) + sizeof(FixedSizedPayload));
  ASSERT_EQ(frame.body_len(), sizeof(FixedSizedPayload));
}

TEST(Frame, TooShortBuffer_NoHdr) {
  // Construct initial frame which has not space to hold a header.
  size_t pkt_len = sizeof(TestHdr1) - 1;
  auto pkt = GetPacket(pkt_len);
}

TEST(Frame, RxInfo_MacFrame) {
  // Construct a large Packet which holds wlan_rx_info_t
  auto pkt = GetPacket(128);
  wlan_rx_info_t rx_info{.data_rate = 1337};
  pkt->CopyCtrlFrom(rx_info);

  // Only MAC frames can hold rx_info;
  MgmtFrame<> mgmt_frame(std::move(pkt));
  ASSERT_TRUE(mgmt_frame.View().has_rx_info());
  ASSERT_EQ(memcmp(mgmt_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);

  CtrlFrame<PsPollFrame> ctrl_frame(mgmt_frame.Take());
  ASSERT_TRUE(ctrl_frame.View().has_rx_info());
  ASSERT_EQ(memcmp(ctrl_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);

  MgmtFrame<> data_frame(ctrl_frame.Take());
  ASSERT_TRUE(data_frame.View().has_rx_info());
  ASSERT_EQ(memcmp(data_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);
}

TEST(Frame, RxInfo_OtherFrame) {
  // Construct a large Packet which holds wlan_rx_info_t
  auto pkt = GetPacket(128);
  wlan_rx_info_t rx_info;
  pkt->CopyCtrlFrom(rx_info);

  // Only MAC frames can hold rx_info. Test some others.
  Frame<TestHdr1> frame1(std::move(pkt));
  ASSERT_FALSE(frame1.View().has_rx_info());
  Frame<TestHdr1> frame2(frame1.Take());
  ASSERT_FALSE(frame2.View().has_rx_info());
  Frame<Beacon> frame3(frame2.Take());
  ASSERT_FALSE(frame3.View().has_rx_info());
  Frame<FrameControl> frame4(frame3.Take());
  ASSERT_FALSE(frame4.View().has_rx_info());
  Frame<uint8_t> frame5(frame4.Take());
  ASSERT_FALSE(frame5.View().has_rx_info());
}

TEST(Frame, RxInfo_PaddingAlignedBody) {
  // Construct frame which holds wlan_rx_info_t and uses additional padding.
  auto pkt = GetPacket(128);
  wlan_rx_info_t rx_info{.rx_flags = WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4};
  pkt->CopyCtrlFrom(rx_info);
  auto hdr = pkt->mut_field<DataFrameHeader>(0);
  // Adjust header to hold 4 addresses which changes the header's length to 30
  // bytes instead of 24. This will then cause additional padding for 4-byte
  // alignment.
  hdr->fc.set_to_ds(1);
  hdr->fc.set_from_ds(1);
  ASSERT_EQ(hdr->len(), static_cast<size_t>(30));
  // Body should follow after an additional 2 byte padding.
  auto data = pkt->mut_field<uint8_t>(hdr->len() + 2);
  data[0] = 42;

  DataFrame<> data_frame(std::move(pkt));
  ASSERT_TRUE(data_frame.View().has_rx_info());
  ASSERT_EQ(memcmp(data_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);
  ASSERT_EQ(data_frame.body_data()[0], 42);
}

TEST(Frame, RxInfo_NoPaddingAlignedBody) {
  // Construct frame which holds wlan_rx_info_t and uses additional padding.
  auto pkt = GetPacket(128);
  wlan_rx_info_t rx_info{.rx_flags = 0};
  pkt->CopyCtrlFrom(rx_info);
  auto hdr = pkt->mut_field<DataFrameHeader>(0);
  // Adjust header to hold 4 addresses which changes the header's length to 30
  // bytes instead of 24. Because rx_info's padding bit is not flipped, the body
  // should not be 4-byte aligned and thus directly follow the header.
  hdr->fc.set_to_ds(1);
  hdr->fc.set_from_ds(1);
  ASSERT_EQ(hdr->len(), static_cast<size_t>(30));
  // Body should follow after an additional 2 byte padding.
  auto data = pkt->mut_field<uint8_t>(hdr->len());
  data[0] = 42;

  DataFrame<> data_frame(std::move(pkt));
  ASSERT_TRUE(data_frame.View().has_rx_info());
  ASSERT_EQ(memcmp(data_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);
  ASSERT_EQ(data_frame.body_data()[0], 42);
}

TEST(Frame, ConstructEmptyFrame) {
  Frame<TestHdr1> frame;
  ASSERT_TRUE(frame.IsEmpty());
}

TEST(Frame, AdvanceThroughAmsduFrame) {
  constexpr size_t kPadding = 2;

  auto frame_data = test_data::kAmsduDataFrame;
  auto pkt = GetPacket(frame_data.size());
  pkt->CopyFrom(frame_data.data(), frame_data.size(), 0);

  auto opt_data_frame = DataFrameView<>::CheckType(pkt.get());
  ASSERT_TRUE(opt_data_frame);
  auto data_frame = opt_data_frame.CheckLength();
  ASSERT_TRUE(data_frame);

  auto opt_data_amsdu_frame = data_frame.CheckBodyType<AmsduSubframeHeader>();
  ASSERT_TRUE(opt_data_amsdu_frame);
  auto data_amsdu_frame = opt_data_amsdu_frame.CheckLength();
  ASSERT_TRUE(data_amsdu_frame);

  auto amsdu_subframe1 = data_amsdu_frame.SkipHeader();
  ASSERT_TRUE(amsdu_subframe1);
  auto opt_amsdu_llc_subframe1 = amsdu_subframe1.CheckBodyType<LlcHeader>();
  ASSERT_TRUE(opt_amsdu_llc_subframe1);
  auto amsdu_llc_subframe1 = opt_amsdu_llc_subframe1.CheckLength();
  ASSERT_TRUE(amsdu_llc_subframe1);

  size_t msdu_len = amsdu_llc_subframe1.hdr()->msdu_len();
  ASSERT_EQ(msdu_len, static_cast<uint16_t>(116));
  auto llc_frame = amsdu_llc_subframe1.SkipHeader();
  ASSERT_TRUE(llc_frame);

  auto opt_amsdu_llc_subframe2 = llc_frame.AdvanceBy(msdu_len + kPadding).As<AmsduSubframeHeader>();
  ASSERT_TRUE(opt_amsdu_llc_subframe2);
  auto amsdu_llc_subframe2 = opt_amsdu_llc_subframe2.CheckLength();
  ASSERT_TRUE(amsdu_llc_subframe2);

  msdu_len = amsdu_llc_subframe2.hdr()->msdu_len();
  ASSERT_EQ(msdu_len, static_cast<uint16_t>(102));
}

TEST(Frame, AdvanceThroughEmptyFrame) {
  MgmtFrameView<> empty_frame;
  ASSERT_FALSE(empty_frame);
  ASSERT_FALSE(empty_frame.SkipHeader());
  ASSERT_FALSE(empty_frame.CheckBodyType<Beacon>());
  ASSERT_FALSE(empty_frame.AdvanceBy(5));
  ASSERT_FALSE(empty_frame.As<DataFrameHeader>());
}

TEST(Frame, AdvanceOutOfBounds) {
  auto pkt = GetPacket(20);
  DataFrameView<> frame(pkt.get());
  ASSERT_TRUE(frame);

  ASSERT_TRUE(frame.AdvanceBy(20));
  ASSERT_FALSE(frame.AdvanceBy(21));
}

TEST(Frame, AdvanceThroughEapolFrame) {
  // The test frame uses padding after it's data header.
  // Setup a Packet which respects this.
  auto frame_data = test_data::kDataLlcEapolFrame;
  auto pkt = GetPacket(frame_data.size());
  pkt->CopyFrom(frame_data.data(), frame_data.size(), 0);
  wlan_rx_info_t rx_info{.rx_flags = WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4};
  pkt->CopyCtrlFrom(rx_info);

  auto opt_data_frame = DataFrameView<>::CheckType(pkt.get());
  ASSERT_TRUE(opt_data_frame);
  auto data_frame = opt_data_frame.CheckLength();
  ASSERT_TRUE(data_frame);

  auto opt_data_llc_frame = data_frame.CheckBodyType<LlcHeader>();
  ASSERT_TRUE(opt_data_llc_frame);
  auto data_llc_frame = opt_data_llc_frame.CheckLength();
  ASSERT_TRUE(data_llc_frame);
  ASSERT_EQ(data_llc_frame.body()->protocol_id(), kEapolProtocolId);

  auto llc_frame = data_llc_frame.SkipHeader();
  ASSERT_TRUE(llc_frame);
  ASSERT_EQ(llc_frame.hdr()->protocol_id(), kEapolProtocolId);
  auto opt_llc_eapol_frame = llc_frame.CheckBodyType<EapolHdr>();
  ASSERT_TRUE(opt_llc_eapol_frame);
  auto llc_eapol_frame = opt_llc_eapol_frame.CheckLength();
  ASSERT_TRUE(llc_eapol_frame);

  auto eapol_frame = llc_eapol_frame.SkipHeader();
  ASSERT_TRUE(eapol_frame);
  ASSERT_EQ(eapol_frame.hdr()->packet_type, 0x03);
}

TEST(Frame, EmptyBodyData) {
  size_t pkt_len = sizeof(TestHdr1);
  auto pkt = GetPacket(pkt_len);
  Frame<TestHdr1> frame(std::move(pkt));
  ASSERT_TRUE(frame.body_data().empty());
}

TEST(Frame, PopulatedBodyData) {
  size_t pkt_len = sizeof(TestHdr1) + 10;
  auto pkt = GetPacket(pkt_len);
  Frame<TestHdr1> frame(std::move(pkt));
  ASSERT_EQ(frame.body_data().size_bytes(), 10lu);
}

TEST(Frame, DdkConversion) {
  // DDK uint32_t to class CapabilityInfo
  uint32_t ddk_caps = 0;
  auto ieee_caps = CapabilityInfo::FromDdk(ddk_caps);
  EXPECT_EQ(0, ieee_caps.val());

  ddk_caps |= WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE;
  ieee_caps = CapabilityInfo::FromDdk(ddk_caps);
  EXPECT_EQ(1, ieee_caps.short_preamble());
  EXPECT_EQ(0, ieee_caps.spectrum_mgmt());
  EXPECT_EQ(0, ieee_caps.short_slot_time());
  EXPECT_EQ(0, ieee_caps.radio_msmt());
  EXPECT_EQ(0x0020, ieee_caps.val());

  ddk_caps =
      WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE | WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME;
  ieee_caps = CapabilityInfo::FromDdk(ddk_caps);
  EXPECT_EQ(1, ieee_caps.short_preamble());
  EXPECT_EQ(0, ieee_caps.spectrum_mgmt());
  EXPECT_EQ(1, ieee_caps.short_slot_time());
  EXPECT_EQ(0, ieee_caps.radio_msmt());
  EXPECT_EQ(0x420, ieee_caps.val());
}

TEST(Frame, ParseProbeRequests) {
  auto frame_data = test_data::kProbeRequestFrame;
  auto pkt = GetPacket(frame_data.size());
  pkt->CopyFrom(frame_data.data(), frame_data.size(), 0);

  MgmtFrame<ProbeRequest> probe_req(std::move(pkt));
  uint8_t expected_ie_chain[] = {
      0x00, 0x00, 0x01, 0x08, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c, 0x2d, 0x1a,
      0xef, 0x01, 0x13, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x09,
      0x04, 0x00, 0x0a, 0x02, 0x01, 0x00, 0x00, 0x40, 0x80, 0xbf, 0x0c, 0xb2, 0x79, 0x91,
      0x33, 0xfa, 0xff, 0x0c, 0x03, 0xfa, 0xff, 0x0c, 0x03, 0xdd, 0x07, 0x00, 0x50, 0xf2,
      0x08, 0x00, 0x23, 0x00, 0xff, 0x03, 0x02, 0x00, 0x1c};
  EXPECT_RANGES_EQ(probe_req.body_data(), expected_ie_chain);
}

}  // namespace
}  // namespace wlan
