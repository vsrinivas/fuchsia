// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/frame_dispatcher.h>

#include "test_data.h"

#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

#include <fbl/unique_ptr.h>
#include <gtest/gtest.h>

namespace wlan {

namespace {

#define IMPL_HANDLE_FRAME(method, frametype)                                       \
    uint64_t handled##method = 0;                                                  \
    zx_status_t Handle##method(const frametype& frame) override {                  \
        handled##method++;                                                         \
        /* TODO(hahnr): const_cast is temporary until Frame Handler 2.0 landed. */ \
        auto frame_ptr = const_cast<frametype*>(&frame);                           \
        handled_packet = frame_ptr->Take();                                        \
        return ZX_OK;                                                              \
    }                                                                              \
    uint64_t Dispatch##method(const std::vector<uint8_t>& frame_data,              \
                              Packet::Peer pkt_peer = Packet::Peer::kWlan) {       \
        handled##method = 0;                                                       \
        DispatchFrame(frame_data, pkt_peer);                                       \
        return handled##method;                                                    \
    }

struct FrameDispatchTest : public ::testing::Test, public FrameHandler {
    IMPL_HANDLE_FRAME(Beacon, MgmtFrame<Beacon>);
    IMPL_HANDLE_FRAME(PsPollFrame, CtrlFrame<PsPollFrame>);
    IMPL_HANDLE_FRAME(Deauthentication, MgmtFrame<Deauthentication>);
    IMPL_HANDLE_FRAME(DataFrame, DataFrame<LlcHeader>);
    IMPL_HANDLE_FRAME(NullDataFrame, DataFrame<NullDataHdr>);
    IMPL_HANDLE_FRAME(EthFrame, EthFrame);

    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override {
        mgmt_hdr.reset(new MgmtFrameHeader);
        *mgmt_hdr = hdr;
        return ZX_OK;
    }
    zx_status_t HandleDataFrame(const DataFrameHeader& hdr) override {
        data_hdr.reset(new DataFrameHeader);
        *data_hdr = hdr;
        return ZX_OK;
    }
    zx_status_t HandleCtrlFrame(const FrameControl& hdr) override {
        ctrl_hdr.reset(new FrameControl);
        *ctrl_hdr = hdr;
        return ZX_OK;
    }

    void DispatchFrame(const std::vector<uint8_t>& frame_data,
                       Packet::Peer pkt_peer = Packet::Peer::kWlan) {
        handled_packet = nullptr;
        mgmt_hdr = nullptr;
        data_hdr = nullptr;
        ctrl_hdr = nullptr;

        auto buffer = GetBuffer(frame_data.size());
        auto pkt = fbl::make_unique<Packet>(fbl::move(buffer), frame_data.size());
        pkt->set_peer(pkt_peer);
        if (pkt_peer == Packet::Peer::kWlan) { pkt->CopyCtrlFrom(wlan_rx_info_t{}); }
        memcpy(pkt->mut_data(), frame_data.data(), frame_data.size());
        pkt->set_peer(pkt_peer);
        DispatchFramePacket(fbl::move(pkt), this);
    }

    fbl::unique_ptr<Packet> handled_packet = nullptr;
    fbl::unique_ptr<MgmtFrameHeader> mgmt_hdr = nullptr;
    fbl::unique_ptr<DataFrameHeader> data_hdr = nullptr;
    fbl::unique_ptr<FrameControl> ctrl_hdr = nullptr;
};

TEST_F(FrameDispatchTest, HandleBeacon) {
    uint64_t handled = DispatchBeacon(kBeaconFrame);
    EXPECT_EQ(handled, static_cast<uint64_t>(1));
    EXPECT_TRUE(mgmt_hdr != nullptr);
    EXPECT_TRUE(ctrl_hdr == nullptr);
    EXPECT_TRUE(data_hdr == nullptr);
    ASSERT_TRUE(handled_packet != nullptr);
    EXPECT_EQ(memcmp(kBeaconFrame.data(), handled_packet->data(), kBeaconFrame.size()), 0);
}

TEST_F(FrameDispatchTest, HandlePsPoll) {
    uint64_t handled = DispatchPsPollFrame(kPsPollFrame);
    EXPECT_EQ(handled, static_cast<uint64_t>(1));
    EXPECT_TRUE(ctrl_hdr != nullptr);
    EXPECT_TRUE(mgmt_hdr == nullptr);
    EXPECT_TRUE(data_hdr == nullptr);
    ASSERT_TRUE(handled_packet != nullptr);
    EXPECT_EQ(memcmp(kPsPollFrame.data(), handled_packet->data(), kPsPollFrame.size()), 0);
}

TEST_F(FrameDispatchTest, HandlePsPollUnsupported) {
    uint64_t handled = DispatchPsPollFrame(kPsPollHtcUnsupportedFrame);
    EXPECT_EQ(handled, static_cast<uint64_t>(0));
    EXPECT_TRUE(handled_packet == nullptr);
    EXPECT_TRUE(mgmt_hdr == nullptr);
    EXPECT_TRUE(ctrl_hdr == nullptr);
    EXPECT_TRUE(data_hdr == nullptr);
}

TEST_F(FrameDispatchTest, HandleDeauthentication) {
    uint64_t handled = DispatchDeauthentication(kDeauthFrame);
    EXPECT_EQ(handled, static_cast<uint64_t>(1));
    EXPECT_TRUE(mgmt_hdr != nullptr);
    EXPECT_TRUE(ctrl_hdr == nullptr);
    EXPECT_TRUE(data_hdr == nullptr);
    ASSERT_TRUE(handled_packet != nullptr);
    EXPECT_EQ(memcmp(kDeauthFrame.data(), handled_packet->data(), kDeauthFrame.size()), 0);
}

TEST_F(FrameDispatchTest, HandleDataFrame) {
    uint64_t handled = DispatchDataFrame(kDataFrame);
    EXPECT_EQ(handled, static_cast<uint64_t>(1));
    EXPECT_TRUE(data_hdr != nullptr);
    EXPECT_TRUE(ctrl_hdr == nullptr);
    EXPECT_TRUE(mgmt_hdr == nullptr);
    ASSERT_TRUE(handled_packet != nullptr);
    EXPECT_EQ(memcmp(kDataFrame.data(), handled_packet->data(), kDataFrame.size()), 0);
}

TEST_F(FrameDispatchTest, HandleNullDataFrame) {
    uint64_t handled = DispatchNullDataFrame(kNullDataFrame);
    EXPECT_EQ(handled, static_cast<uint64_t>(1));
    EXPECT_TRUE(data_hdr != nullptr);
    EXPECT_TRUE(ctrl_hdr == nullptr);
    EXPECT_TRUE(mgmt_hdr == nullptr);
    ASSERT_TRUE(handled_packet != nullptr);
    EXPECT_EQ(memcmp(kNullDataFrame.data(), handled_packet->data(), kNullDataFrame.size()), 0);
}

TEST_F(FrameDispatchTest, HandleEthFrame) {
    uint64_t handled = DispatchEthFrame(kEthernetFrame, Packet::Peer::kEthernet);
    EXPECT_EQ(handled, static_cast<uint64_t>(1));
    EXPECT_TRUE(data_hdr == nullptr);
    EXPECT_TRUE(ctrl_hdr == nullptr);
    EXPECT_TRUE(mgmt_hdr == nullptr);
    ASSERT_TRUE(handled_packet != nullptr);
    EXPECT_EQ(memcmp(kEthernetFrame.data(), handled_packet->data(), kEthernetFrame.size()), 0);
}

#undef IMPL_HANDLE_FRAME

}  // namespace
}  // namespace wlan
