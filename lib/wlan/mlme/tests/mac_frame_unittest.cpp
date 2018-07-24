// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/wlan.h>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

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
template <size_t padding_len, size_t payload_len> struct TripleHdrFrame {
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

static fbl::unique_ptr<Packet> GetPacket(size_t len) {
    auto buffer = GetBuffer(len);
    memset(buffer->data(), 0, len);
    return fbl::make_unique<Packet>(fbl::move(buffer), len);
}

using DefaultTripleHdrFrame = TripleHdrFrame<0, 10>;
using PaddedTripleHdrFrame = TripleHdrFrame<4, 10>;

TEST(ProbeRequest, Validate) {
    uint8_t buf[128];
    ElementWriter writer(buf, sizeof(buf));

    ASSERT_TRUE(writer.write<SsidElement>("test ssid"));

    std::vector<uint8_t> rates{2, 4, 11, 22};
    ASSERT_TRUE(writer.write<SupportedRatesElement>(rates));

    auto probe_request = FromBytes<ProbeRequest>(buf, writer.size());
    EXPECT_TRUE(probe_request->Validate(writer.size()));
}

TEST(ProbeRequest, OutOfOrderElements) {
    uint8_t buf[128];
    ElementWriter writer(buf, sizeof(buf));

    std::vector<uint8_t> rates{2, 4, 11, 22};
    ASSERT_TRUE(writer.write<SupportedRatesElement>(rates));

    ASSERT_TRUE(writer.write<SsidElement>("test ssid"));

    auto probe_request = FromBytes<ProbeRequest>(buf, writer.size());
    EXPECT_FALSE(probe_request->Validate(writer.size()));
}

TEST(ProbeRequest, InvalidElement) {
    uint8_t buf[128];
    ElementWriter writer(buf, sizeof(buf));

    ASSERT_TRUE(writer.write<SsidElement>("test ssid"));
    ASSERT_TRUE(writer.write<CfParamSetElement>(1, 2, 3, 4));

    auto probe_request = FromBytes<ProbeRequest>(buf, writer.size());
    EXPECT_FALSE(probe_request->Validate(writer.size()));
}

TEST(Frame, General) {
    // Construct initial frame
    auto pkt = GetPacket(DefaultTripleHdrFrame::len());
    auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
    test_frame->hdr1.a = 42;
    test_frame->hdr2.b = 24;

    // Verify frame's accessors and length.
    Frame<TestHdr1> frame(fbl::move(pkt));
    ASSERT_TRUE(frame.HasValidLen());
    ASSERT_FALSE(frame.IsEmpty());
    ASSERT_EQ(frame.len(), DefaultTripleHdrFrame::len());
    ASSERT_EQ(frame.hdr()->a, 42);
    ASSERT_EQ(frame.body_len(), DefaultTripleHdrFrame::body_len());
    ASSERT_EQ(frame.body()->data[1], 24);
}

TEST(Frame, General_Const_Frame) {
    // Construct initial frame.
    auto pkt = GetPacket(DefaultTripleHdrFrame::len());
    auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
    test_frame->hdr1.a = 42;
    test_frame->hdr2.b = 24;

    // Verify a constant frame's accessors and length. Constant accessors differ from regular ones.
    const Frame<TestHdr1> frame(fbl::move(pkt));
    ASSERT_TRUE(frame.HasValidLen());
    ASSERT_FALSE(frame.IsEmpty());
    ASSERT_EQ(frame.len(), DefaultTripleHdrFrame::len());
    ASSERT_EQ(frame.hdr()->a, 42);
    ASSERT_EQ(frame.body_len(), DefaultTripleHdrFrame::body_len());
    ASSERT_EQ(frame.body()->data[1], 24);
}

TEST(Frame, Take) {
    // Construct initial frame.
    auto pkt = GetPacket(DefaultTripleHdrFrame::len());
    auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
    test_frame->hdr1.a = 42;
    test_frame->hdr2.b = 24;

    // Derive frame with unknown body...
    Frame<TestHdr1> frame(fbl::move(pkt));
    // ... and take the frame's underlying Packet to construct a new, specialized frame.
    Frame<TestHdr1, TestHdr2> specialized_frame(frame.Take());
    // Verify the first frame is considered "taken" and that the new specialized one is valid.
    ASSERT_TRUE(frame.IsEmpty());
    ASSERT_FALSE(frame.HasValidLen());
    ASSERT_TRUE(specialized_frame.HasValidLen());
    ASSERT_FALSE(specialized_frame.IsEmpty());
    ASSERT_EQ(specialized_frame.len(), DefaultTripleHdrFrame::len());
    ASSERT_EQ(specialized_frame.hdr()->a, 42);
    ASSERT_EQ(specialized_frame.body_len(), DefaultTripleHdrFrame::body_len());
    ASSERT_EQ(specialized_frame.body()->b, 24);
}

TEST(Frame, NextFrame) {
    // Construct initial frame.
    auto pkt = GetPacket(DefaultTripleHdrFrame::len());
    auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
    test_frame->hdr2.b = 24;
    test_frame->hdr3.a = 42;

    // Start with first header.
    Frame<TestHdr1, TestHdr2> frame(fbl::move(pkt));

    // Access second frame with unknown body. Verify correct length and accessors.
    auto second_frame = frame.NextFrame<TestHdr2>();
    ASSERT_TRUE(frame.IsEmpty());
    ASSERT_TRUE(second_frame.HasValidLen());
    ASSERT_FALSE(second_frame.IsEmpty());
    ASSERT_EQ(second_frame.len(), DefaultTripleHdrFrame::second_frame_len());
    ASSERT_EQ(second_frame.body_len(), DefaultTripleHdrFrame::second_frame_body_len());
    ASSERT_EQ(second_frame.hdr()->b, 24);

    // Access the third frame with unknown body. Verify correct length and accessors.
    auto third_frame = second_frame.NextFrame<TestHdr3>();
    ASSERT_TRUE(second_frame.IsEmpty());
    ASSERT_TRUE(third_frame.HasValidLen());
    ASSERT_FALSE(third_frame.IsEmpty());
    ASSERT_EQ(third_frame.len(), DefaultTripleHdrFrame::third_frame_len());
    ASSERT_EQ(third_frame.body_len(), DefaultTripleHdrFrame::third_frame_body_len());
    ASSERT_EQ(third_frame.hdr()->a, 42);
}

TEST(Frame, NextFrame_FullSpecialized) {
    // Construct initial frame.
    auto pkt = GetPacket(DefaultTripleHdrFrame::len());
    auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
    test_frame->hdr2.b = 24;
    test_frame->hdr3.a = 42;

    // Start with first header.
    Frame<TestHdr1> frame(fbl::move(pkt));

    // Access second frame. Verify correct length and accessors.
    auto second_frame = frame.NextFrame<TestHdr2, TestHdr3>();
    ASSERT_TRUE(frame.IsEmpty());
    ASSERT_TRUE(second_frame.HasValidLen());
    ASSERT_FALSE(second_frame.IsEmpty());
    ASSERT_EQ(second_frame.len(), DefaultTripleHdrFrame::second_frame_len());
    ASSERT_EQ(second_frame.body_len(), DefaultTripleHdrFrame::second_frame_body_len());
    ASSERT_EQ(second_frame.hdr()->b, 24);
    ASSERT_EQ(second_frame.body()->a, 42);
}

TEST(Frame, NextFrame_DynamicSized) {
    // Construct initial frame. This frame will compute the second frame's header length
    // dynamically.
    auto pkt = GetPacket(PaddedTripleHdrFrame::len());
    auto test_frame = pkt->mut_field<PaddedTripleHdrFrame>(0);
    test_frame->hdr2.has_padding = true;
    test_frame->hdr3.a = 42;

    Frame<TestHdr1> frame(fbl::move(pkt));
    auto second_frame = frame.NextFrame<TestHdr2>();
    ASSERT_TRUE(frame.IsEmpty());
    ASSERT_TRUE(second_frame.HasValidLen());
    ASSERT_FALSE(second_frame.IsEmpty());
    ASSERT_EQ(second_frame.len(), PaddedTripleHdrFrame::second_frame_len());
    ASSERT_EQ(second_frame.body_len(), PaddedTripleHdrFrame::second_frame_body_len());

    // Third frame should be accessible as expected.
    auto third_frame = second_frame.NextFrame<TestHdr3>();
    ASSERT_TRUE(second_frame.IsEmpty());
    ASSERT_TRUE(third_frame.HasValidLen());
    ASSERT_FALSE(third_frame.IsEmpty());
    ASSERT_EQ(third_frame.len(), PaddedTripleHdrFrame::third_frame_len());
    ASSERT_EQ(third_frame.body_len(), PaddedTripleHdrFrame::third_frame_body_len());
    ASSERT_EQ(third_frame.hdr()->a, 42);
}

TEST(Frame, ExactlySizedBuffer_HdrOnly) {
    // Construct initial frame which has just enough space to hold a header.
    size_t pkt_len = sizeof(TestHdr1);
    auto pkt = GetPacket(pkt_len);

    Frame<TestHdr1> frame(fbl::move(pkt));
    ASSERT_TRUE(frame.HasValidLen());
    ASSERT_EQ(frame.len(), sizeof(TestHdr1));
    ASSERT_EQ(frame.body_len(), static_cast<size_t>(0));
}

TEST(Frame, ExactlySizedBuffer_Frame) {
    // Construct initial frame which has just enough space to hold a header and a body.
    size_t pkt_len = sizeof(TestHdr1) + sizeof(FixedSizedPayload);
    auto pkt = GetPacket(pkt_len);

    Frame<TestHdr1, FixedSizedPayload> frame(fbl::move(pkt));
    ASSERT_TRUE(frame.HasValidLen());
    ASSERT_EQ(frame.len(), sizeof(TestHdr1) + sizeof(FixedSizedPayload));
    ASSERT_EQ(frame.body_len(), sizeof(FixedSizedPayload));
}

TEST(Frame, TooShortBuffer_NoHdr) {
    // Construct initial frame which has not space to hold a header.
    size_t pkt_len = sizeof(TestHdr1) - 1;
    auto pkt = GetPacket(pkt_len);

    Frame<TestHdr1> frame(fbl::move(pkt));
    ASSERT_FALSE(frame.HasValidLen());
}

TEST(Frame, TooShortBuffer_Hdr_NoBody) {
    // Construct initial frame which has just enough space to hold a header but not enough to hold
    // an entire body.
    size_t pkt_len = sizeof(TestHdr1) + sizeof(FixedSizedPayload) - 1;
    auto pkt = GetPacket(pkt_len);

    Frame<TestHdr1> frame(fbl::move(pkt));
    ASSERT_TRUE(frame.HasValidLen());

    Frame<TestHdr1, FixedSizedPayload> specialized_frame(frame.Take());
    ASSERT_FALSE(specialized_frame.HasValidLen());
}

TEST(Frame, TrimExcessBodyLength) {
    // Construct initial frame which can hold three headers.
    auto pkt = GetPacket(DefaultTripleHdrFrame::len());
    Frame<TestHdr1, TestHdr2> frame(fbl::move(pkt));
    ASSERT_TRUE(frame.HasValidLen());

    // Trim excess body length. Frame should now be the size of two headers.
    auto status = frame.set_body_len(sizeof(TestHdr2));
    ASSERT_EQ(status, ZX_OK);
    ASSERT_TRUE(frame.HasValidLen());
    ASSERT_EQ(frame.len(), sizeof(TestHdr1) + sizeof(TestHdr2));
    ASSERT_EQ(frame.body_len(), sizeof(TestHdr2));

    // Verify sub frame is also correct.
    auto next_frame = frame.NextFrame();
    ASSERT_TRUE(next_frame.HasValidLen());
    ASSERT_EQ(next_frame.len(), sizeof(TestHdr2));
    ASSERT_EQ(next_frame.body_len(), static_cast<size_t>(0));

    // Verify the frame which originally had sufficient space doesn't fit anymore.
    Frame<TestHdr2, TestHdr3> specialized_next_frame(sizeof(TestHdr1), next_frame.Take());
    ASSERT_FALSE(specialized_next_frame.HasValidLen());
}

TEST(Frame, RxInfo_MacFrame) {
    // Construct a large Packet which holds wlan_rx_info_t
    auto pkt = GetPacket(128);
    wlan_rx_info_t rx_info{.data_rate = 1337};
    pkt->CopyCtrlFrom(rx_info);

    // Only MAC frames can hold rx_info;
    MgmtFrame<> mgmt_frame(fbl::move(pkt));
    ASSERT_TRUE(mgmt_frame.View().has_rx_info());
    ASSERT_EQ(memcmp(mgmt_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);

    CtrlFrame<PsPollFrame> ctrl_frame(mgmt_frame.Take());
    ASSERT_TRUE(ctrl_frame.View().has_rx_info());
    ASSERT_EQ(memcmp(ctrl_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);

    MgmtFrame<> data_frame(ctrl_frame.Take());
    ASSERT_TRUE(data_frame.View().has_rx_info());
    ASSERT_EQ(memcmp(data_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);

    // Verify next frame does not hold an rx_info.
    auto next_frame = data_frame.NextFrame<LlcHeader>();
    ASSERT_FALSE(next_frame.View().has_rx_info());
}

TEST(Frame, RxInfo_OtherFrame) {
    // Construct a large Packet which holds wlan_rx_info_t
    auto pkt = GetPacket(128);
    wlan_rx_info_t rx_info;
    pkt->CopyCtrlFrom(rx_info);

    // Only MAC frames can hold rx_info. Test some others.
    Frame<TestHdr1> frame1(fbl::move(pkt));
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
    // Adjust header to hold 4 addresses which changes the header's length to 30 bytes instead
    // of 24. This will then cause additional padding for 4-byte alignment.
    hdr->fc.set_to_ds(1);
    hdr->fc.set_from_ds(1);
    ASSERT_EQ(hdr->len(), static_cast<size_t>(30));
    // Body should follow after an additional 2 byte padding.
    auto data = pkt->mut_field<uint8_t>(hdr->len() + 2);
    data[0] = 42;

    DataFrame<> data_frame(fbl::move(pkt));
    ASSERT_TRUE(data_frame.View().has_rx_info());
    ASSERT_EQ(memcmp(data_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);
    ASSERT_EQ(data_frame.body()->data[0], 42);
}

TEST(Frame, RxInfo_NoPaddingAlignedBody) {
    // Construct frame which holds wlan_rx_info_t and uses additional padding.
    auto pkt = GetPacket(128);
    wlan_rx_info_t rx_info{.rx_flags = 0};
    pkt->CopyCtrlFrom(rx_info);
    auto hdr = pkt->mut_field<DataFrameHeader>(0);
    // Adjust header to hold 4 addresses which changes the header's length to 30 bytes instead
    // of 24. Because rx_info's padding bit is not flipped, the body should not be 4-byte aligned
    // and thus directly follow the header.
    hdr->fc.set_to_ds(1);
    hdr->fc.set_from_ds(1);
    ASSERT_EQ(hdr->len(), static_cast<size_t>(30));
    // Body should follow after an additional 2 byte padding.
    auto data = pkt->mut_field<uint8_t>(hdr->len());
    data[0] = 42;

    DataFrame<> data_frame(fbl::move(pkt));
    ASSERT_TRUE(data_frame.View().has_rx_info());
    ASSERT_EQ(memcmp(data_frame.View().rx_info(), &rx_info, sizeof(wlan_rx_info_t)), 0);
    ASSERT_EQ(data_frame.body()->data[0], 42);
}

TEST(Frame, ConstructEmptyFrame) {
    Frame<TestHdr1> frame;
    ASSERT_TRUE(frame.IsEmpty());
    ASSERT_FALSE(frame.HasValidLen());
}

TEST(Frame, Specialize) {
    // Construct initial frame
    auto pkt = GetPacket(DefaultTripleHdrFrame::len());
    auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
    test_frame->hdr1.a = 42;
    test_frame->hdr2.b = 24;

    // Verify frame's accessors and length.
    Frame<TestHdr1> frame(fbl::move(pkt));
    Frame<TestHdr1, TestHdr2> specialized_frame = frame.Specialize<TestHdr2>();
    ASSERT_TRUE(specialized_frame.HasValidLen());
    ASSERT_FALSE(specialized_frame.IsEmpty());
    ASSERT_TRUE(frame.IsEmpty());
    ASSERT_EQ(specialized_frame.len(), DefaultTripleHdrFrame::len());
    ASSERT_EQ(specialized_frame.hdr()->a, 42);
    ASSERT_EQ(specialized_frame.body_len(), DefaultTripleHdrFrame::body_len());
    ASSERT_EQ(specialized_frame.body()->b, 24);
}

TEST(Frame, Specialize_ProgressedFrame) {
    // Construct initial frame
    auto pkt = GetPacket(DefaultTripleHdrFrame::len());
    auto test_frame = pkt->mut_field<DefaultTripleHdrFrame>(0);
    test_frame->hdr1.a = 42;
    test_frame->hdr2.b = 24;
    test_frame->hdr3.b = 1337;

    // Verify frame's accessors and length.
    Frame<TestHdr1> frame(fbl::move(pkt));
    Frame<TestHdr2, UnknownBody> second_frame = frame.NextFrame<TestHdr2>();
    Frame<TestHdr2, TestHdr3> specialized_frame = second_frame.Specialize<TestHdr3>();
    ASSERT_TRUE(specialized_frame.HasValidLen());
    ASSERT_FALSE(specialized_frame.IsEmpty());
    ASSERT_TRUE(second_frame.IsEmpty());
    ASSERT_EQ(specialized_frame.hdr()->b, 24);
    ASSERT_EQ(specialized_frame.body_len(), DefaultTripleHdrFrame::second_frame_body_len());
    ASSERT_EQ(specialized_frame.body()->b, 1337);
}

}  // namespace
}  // namespace wlan
