// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/buffer_writer.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/ap/beacon_sender.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rates_elements.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include "mock_device.h"
#include "test_bss.h"

#include <gtest/gtest.h>

namespace wlan {
namespace {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

struct MockBss : public BssInterface {
    zx_status_t ScheduleTimeout(wlan_tu_t tus, const common::MacAddr& client_addr, TimeoutId* id) {
        return ZX_OK;
    }

    void CancelTimeout(TimeoutId id) {}

    const common::MacAddr& bssid() const { return bssid_; }
    uint64_t timestamp() { return 0; }

    uint32_t NextSns1(const common::MacAddr& addr) { return 0; }

    std::optional<DataFrame<LlcHeader>> EthToDataFrame(const EthFrame& eth_frame,
                                                       bool needs_protection) {
        return std::nullopt;
    }

    bool IsRsn() const { return false; }
    HtConfig Ht() const { return {}; }
    const Span<const SupportedRate> Rates() const { return {}; }

    zx_status_t SendMgmtFrame(MgmtFrame<>&& mgmt_frame) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t SendDataFrame(DataFrame<>&& data_frame, uint32_t flags) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t DeliverEthernet(Span<const uint8_t> frame) { return ZX_ERR_NOT_SUPPORTED; }

    void OnPreTbtt() {}
    void OnBcnTxComplete() {}

    wlan_channel_t Chan() const { return {}; }

    common::MacAddr bssid_ = common::MacAddr(kBssid1);
};

struct BeaconSenderTest : public ::testing::Test {
    BeaconSenderTest() : bcn_sender(&device) {}

    MockBss bss;
    MockDevice device;
    BeaconSender bcn_sender;
    PsCfg ps_cfg;
};

TEST_F(BeaconSenderTest, Start) {
    ASSERT_FALSE(device.beaconing_enabled);

    bcn_sender.Start(&bss, ps_cfg, CreateStartRequest(false));

    ASSERT_TRUE(device.beaconing_enabled);
    ASSERT_EQ(device.beacon.get(), nullptr);

    bcn_sender.UpdateBeacon(ps_cfg);

    auto pkt = std::move(device.beacon);
    EXPECT_TRUE(device.beaconing_enabled);
    ASSERT_NE(pkt, nullptr);

    auto checked = MgmtFrameView<Beacon>::CheckType(pkt.get());
    ASSERT_TRUE(checked);
    auto beacon_frame = checked.CheckLength();
    ASSERT_TRUE(beacon_frame);
}

TEST_F(BeaconSenderTest, ProbeRequest) {
    bcn_sender.Start(&bss, ps_cfg, CreateStartRequest(false));

    ASSERT_TRUE(device.wlan_queue.empty());

    uint8_t buffer[1024] = {};
    BufferWriter elem_w(buffer);
    common::WriteSsid(&elem_w, kSsid);
    RatesWriter rates_writer{kSupportedRates};
    rates_writer.WriteSupportedRates(&elem_w);
    rates_writer.WriteExtendedSupportedRates(&elem_w);
    common::WriteDsssParamSet(&elem_w, kBssChannel.primary);

    common::MacAddr ra(kClientAddress);
    bcn_sender.SendProbeResponse(ra, elem_w.WrittenData());

    ASSERT_FALSE(device.wlan_queue.empty());
    auto pkt = std::move(*device.wlan_queue.begin());

    auto checked = MgmtFrameView<ProbeResponse>::CheckType(pkt.pkt.get());
    ASSERT_TRUE(checked);
    auto beacon_frame = checked.CheckLength();
    ASSERT_TRUE(beacon_frame);

    EXPECT_EQ(pkt.flags, 0u);
}

TEST(BeaconSender, ShouldSendProbeResponse) {
    const uint8_t our_ssid[] = {'f', 'o', 'o'};

    const uint8_t no_ssid[] = {1, 1, 1};
    EXPECT_TRUE(ShouldSendProbeResponse(no_ssid, our_ssid));

    const uint8_t different_ssid[] = {0, 3, 'b', 'a', 'r', 1, 1, 1};
    EXPECT_FALSE(ShouldSendProbeResponse(different_ssid, our_ssid));

    const uint8_t matching_ssid[] = {0, 3, 'f', 'o', 'o', 1, 1, 1};
    EXPECT_TRUE(ShouldSendProbeResponse(matching_ssid, our_ssid));

    const uint8_t wildcard_ssid[] = {0, 0, 1, 1, 1};
    EXPECT_TRUE(ShouldSendProbeResponse(wildcard_ssid, our_ssid));

    const uint8_t malformed_ssid[35] = {
        0,
        33,
    };
    EXPECT_FALSE(ShouldSendProbeResponse(malformed_ssid, our_ssid));
}

}  // namespace
}  // namespace wlan
