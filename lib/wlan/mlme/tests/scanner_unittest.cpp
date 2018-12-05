// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/scanner.h>

#include "mock_device.h"

#include <lib/timekeeper/clock.h>
#include <wlan/mlme/client/channel_scheduler.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/protocol/mac.h>

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <gtest/gtest.h>
#include <zircon/status.h>

#include <cstring>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace {

const uint8_t kBeacon[] = {
    0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x00, 0x00, 0x09, 0x74, 0x65, 0x73, 0x74, 0x20, 0x73, 0x73, 0x69, 0x64,
};

struct MockOnChannelHandler : OnChannelHandler {
    virtual void HandleOnChannelFrame(fbl::unique_ptr<Packet>) override {}
    virtual void PreSwitchOffChannel() override {}
    virtual void ReturnedOnChannel() override {}
};

class ScannerTest : public ::testing::Test {
   public:
    ScannerTest()
        : chan_sched_(&on_channel_handler_, &mock_dev_, mock_dev_.CreateTimer(1u)),
          scanner_(&mock_dev_, &chan_sched_) {
        mock_dev_.SetChannel(wlan_channel_t{.primary = 11, .cbw = CBW20});
    }

   protected:
    zx_status_t Start(wlan_mlme::ScanRequest&& req) {
        return scanner_.Start({std::move(req), fuchsia_wlan_mlme_MLMEOnScanResultOrdinal});
    }

    void AssertScanEnd(wlan_mlme::ScanResultCodes expected_code) {
        auto scan_ends =
            mock_dev_.GetServiceMsgs<wlan_mlme::ScanEnd>(fuchsia_wlan_mlme_MLMEOnScanEndOrdinal);
        ASSERT_EQ(scan_ends.size(), 1ULL);
        EXPECT_EQ(123u, scan_ends[0].body()->txn_id);
        EXPECT_EQ(expected_code, scan_ends[0].body()->code);
    }

    MockDevice mock_dev_;
    MockOnChannelHandler on_channel_handler_;
    ChannelScheduler chan_sched_;
    Scanner scanner_;
};

wlan_mlme::ScanRequest FakeScanRequest() {
    wlan_mlme::ScanRequest req{};
    req.txn_id = 123;
    req.channel_list.resize(0);
    req.channel_list->push_back(1);
    req.max_channel_time = 1u;
    req.ssid.resize(0);
    return req;
}

TEST_F(ScannerTest, Start) {
    EXPECT_EQ(11u, mock_dev_.GetChannelNumber());
    EXPECT_FALSE(scanner_.IsRunning());

    EXPECT_EQ(ZX_OK, Start(FakeScanRequest()));
    EXPECT_TRUE(scanner_.IsRunning());

    EXPECT_EQ(1u, mock_dev_.GetChannelNumber());
}

TEST_F(ScannerTest, Start_InvalidChannelTimes) {
    auto req = FakeScanRequest();
    req.min_channel_time = 2;
    req.max_channel_time = 1;

    EXPECT_EQ(11u, mock_dev_.GetChannelNumber());

    EXPECT_EQ(ZX_ERR_INVALID_ARGS, Start(std::move(req)));
    EXPECT_FALSE(scanner_.IsRunning());
    EXPECT_EQ(11u, mock_dev_.GetChannelNumber());

    AssertScanEnd(wlan_mlme::ScanResultCodes::INVALID_ARGS);
}

TEST_F(ScannerTest, Start_NoChannels) {
    auto req = FakeScanRequest();
    req.channel_list.resize(0);

    EXPECT_EQ(11u, mock_dev_.GetChannelNumber());

    EXPECT_EQ(ZX_ERR_INVALID_ARGS, Start(std::move(req)));
    EXPECT_FALSE(scanner_.IsRunning());
    EXPECT_EQ(11u, mock_dev_.GetChannelNumber());

    AssertScanEnd(wlan_mlme::ScanResultCodes::INVALID_ARGS);
}

TEST_F(ScannerTest, Reset) {
    ASSERT_EQ(ZX_OK, Start(FakeScanRequest()));
    ASSERT_TRUE(scanner_.IsRunning());

    scanner_.Reset();
    EXPECT_FALSE(scanner_.IsRunning());
    // TODO(tkilbourn): check all the other invariants
}

TEST_F(ScannerTest, ScanChannel) {
    ASSERT_EQ(ZX_OK, Start(FakeScanRequest()));
    auto chan = scanner_.ScanChannel();
    EXPECT_EQ(1u, chan.primary);
}

TEST_F(ScannerTest, Timeout_NextChannel) {
    auto req = FakeScanRequest();
    req.min_channel_time = 1;
    req.max_channel_time = 10;
    req.channel_list.push_back(2);

    EXPECT_EQ(11u, mock_dev_.GetChannelNumber());

    ASSERT_EQ(ZX_OK, Start(std::move(req)));
    ASSERT_EQ(1u, scanner_.ScanChannel().primary);

    EXPECT_EQ(1u, mock_dev_.GetChannelNumber());

    mock_dev_.AdvanceTime(WLAN_TU(req.max_channel_time));
    chan_sched_.HandleTimeout();
    EXPECT_EQ(2u, scanner_.ScanChannel().primary);

    EXPECT_EQ(2u, mock_dev_.GetChannelNumber());
}

TEST_F(ScannerTest, ScanResponse) {
    ASSERT_EQ(ZX_OK, Start(FakeScanRequest()));

    wlan_rx_info_t info;
    info.valid_fields = WLAN_RX_INFO_VALID_RSSI | WLAN_RX_INFO_VALID_SNR;
    info.chan = {
        .primary = 1,
    };
    info.rssi_dbm = -75;
    info.snr_dbh = 30;

    auto buffer = GetBuffer(sizeof(kBeacon));
    auto packet = fbl::make_unique<Packet>(std::move(buffer), sizeof(kBeacon));
    packet->CopyCtrlFrom(info);
    memcpy(packet->mut_field<uint8_t*>(0), kBeacon, sizeof(kBeacon));

    chan_sched_.HandleIncomingFrame(std::move(packet));

    mock_dev_.SetTime(zx::time(1));
    chan_sched_.HandleTimeout();

    auto results =
        mock_dev_.GetServiceMsgs<wlan_mlme::ScanResult>(fuchsia_wlan_mlme_MLMEOnScanResultOrdinal);
    ASSERT_EQ(results.size(), 1ULL);
    wlan_mlme::BSSDescription bss;
    results[0].body()->bss.Clone(&bss);

    EXPECT_EQ(0, std::memcmp(kBeacon + 16, bss.bssid.data(), 6));
    EXPECT_EQ(bss.ssid->size(), static_cast<size_t>(9));

    const uint8_t ssid[] = {'t', 'e', 's', 't', ' ', 's', 's', 'i', 'd'};
    EXPECT_EQ(0, std::memcmp(ssid, bss.ssid->data(), sizeof(ssid)));
    EXPECT_EQ(wlan_mlme::BSSTypes::INFRASTRUCTURE, bss.bss_type);
    EXPECT_EQ(100u, bss.beacon_period);
    EXPECT_EQ(1024u, bss.timestamp);
    // EXPECT_EQ(1u, bss->channel);  // IE missing. info.chan != bss->channel.
    EXPECT_EQ(-75, bss.rssi_dbm);
    EXPECT_EQ(WLAN_RCPI_DBMH_INVALID, bss.rcpi_dbmh);
    EXPECT_EQ(30, bss.rsni_dbh);

    AssertScanEnd(wlan_mlme::ScanResultCodes::SUCCESS);
}

}  // namespace
}  // namespace wlan
