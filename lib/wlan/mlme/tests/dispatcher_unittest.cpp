// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/dispatcher.h>

#include "mock_device.h"

#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <gtest/gtest.h>
#include <functional>

#include <wlan_mlme/cpp/fidl.h>

namespace wlan {
namespace {

template <typename Frame> using FrameCallback = std::function<void(const Frame&)>;
using EthFrameCallback = FrameCallback<ImmutableBaseFrame<EthernetII>>;

struct MockMlme : public Mlme {
    MockMlme() {}
    zx_status_t Init() override final { return ZX_OK; };
    zx_status_t PreChannelChange(wlan_channel_t chan) override final { return ZX_OK; }
    zx_status_t PostChannelChange() override final { return ZX_OK; }
    zx_status_t HandleTimeout(const ObjectId id) override final { return ZX_OK; }

    zx_status_t HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) override final {
        eth_cb_(frame);
        return ZX_OK;
    }

    void SetFrameCallback(EthFrameCallback eth_cb) { eth_cb_ = eth_cb; }

   private:
    EthFrameCallback eth_cb_;
};

class DispatcherTest : public ::testing::Test {
   public:
    template <typename Frame>
    bool HandlePacket(fbl::unique_ptr<Packet> packet, FrameCallback<Frame> cb) {
        bool handled = false;
        auto mock_mlme = fbl::make_unique<MockMlme>();
        mock_mlme->SetFrameCallback([&handled, cb](const Frame& frame) mutable {
            handled = true;
            cb(frame);
        });
        auto dispatcher = fbl::make_unique<Dispatcher>(&mock_dev_, fbl::move(mock_mlme));
        dispatcher->HandlePacket(fbl::move(packet));
        return handled;
    }

   protected:
    MockDevice mock_dev_;
};

static constexpr uint8_t kPayload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static const common::MacAddr kMacAddr1({1, 2, 3, 4, 5, 6});
static const common::MacAddr kMacAddr2({7, 8, 9, 10, 11, 12});

static fbl::unique_ptr<Packet> CreateEthPacket() {
    static constexpr size_t payload_len = sizeof(kPayload);
    static constexpr size_t eth_len = sizeof(EthernetII) + payload_len;

    auto buffer = GetBuffer(eth_len);
    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), eth_len);
    packet->set_peer(Packet::Peer::kEthernet);
    auto eth = packet->mut_field<EthernetII>(0);
    eth->ether_type = 0xABCD;
    eth->dest = kMacAddr1;
    eth->src = kMacAddr2;
    auto body = packet->mut_field<uint8_t>(sizeof(EthernetII));
    memcpy(body, kPayload, payload_len);

    return packet;
}

TEST_F(DispatcherTest, HandleEthPacket) {
    auto packet = CreateEthPacket();

    bool handled = HandlePacket<ImmutableBaseFrame<EthernetII>>(fbl::move(packet), [](auto& frame) {
        EXPECT_EQ(frame.body_len(), sizeof(kPayload));
        EXPECT_EQ(frame.hdr()->ether_type, 0xABCD);
        EXPECT_EQ(frame.hdr()->dest, kMacAddr1);
        EXPECT_EQ(frame.hdr()->src, kMacAddr2);
        EXPECT_EQ(0, std::memcmp(frame.body(), kPayload, frame.body_len()));
    });
    EXPECT_TRUE(handled);
}

}  // namespace
}  // namespace wlan
