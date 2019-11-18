// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/timekeeper/clock.h>
#include <zircon/status.h>

#include <cstring>
#include <memory>

#include <fbl/ref_ptr.h>
#include <gtest/gtest.h>
#include <wlan/common/element_splitter.h>
#include <wlan/mlme/client/channel_scheduler.h>
#include <wlan/mlme/client/scanner.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/protocol/mac.h>

#include "mock_device.h"
#include "test_bss.h"
#include "test_utils.h"

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace {

const uint8_t kBeacon[] = {
    0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x00, 0x00, 0x09, 0x74, 0x65, 0x73, 0x74, 0x20, 0x73, 0x73, 0x69, 0x64,
};

const uint8_t kHiddenApBeacon[] = {
    0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t kProbeResponse[] = {
    0x50, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x00, 0x00, 0x09, 0x74, 0x65, 0x73, 0x74, 0x20, 0x73, 0x73, 0x69, 0x64,
};

struct MockOnChannelHandler : OnChannelHandler {
  virtual void HandleOnChannelFrame(std::unique_ptr<Packet>) override {}
  virtual void PreSwitchOffChannel() override {}
  virtual void ReturnedOnChannel() override {}
};

class ScannerTest : public ::testing::Test {
 public:
  ScannerTest()
      : timer_mgr_(TimerManager<TimeoutTarget>(mock_dev_.CreateTimer(1u))),
        chan_sched_(&on_channel_handler_, &mock_dev_, &timer_mgr_),
        scanner_(&mock_dev_, &chan_sched_, &timer_mgr_) {
    mock_dev_.SetChannel(wlan_channel_t{.primary = 11, .cbw = WLAN_CHANNEL_BANDWIDTH__20});
  }

 protected:
  zx_status_t Start(wlan_mlme::ScanRequest&& req) {
    return scanner_.Start(
        {std::move(req), fuchsia::wlan::mlme::internal::kMLME_OnScanResult_GenOrdinal});
  }

  std::unique_ptr<wlan::Packet> CreatePacket(fbl::Span<const uint8_t> data) {
    wlan_rx_info_t info;
    info.valid_fields = WLAN_RX_INFO_VALID_RSSI | WLAN_RX_INFO_VALID_SNR;
    info.chan = {
        .primary = 1,
    };
    info.rssi_dbm = -75;
    info.snr_dbh = 30;

    auto buffer = GetBuffer(data.size());
    auto packet = std::make_unique<Packet>(std::move(buffer), data.size());
    packet->CopyCtrlFrom(info);
    memcpy(packet->mut_field<uint8_t*>(0), data.data(), data.size());
    return packet;
  }

  void AssertScanResult(const MlmeMsg<wlan_mlme::ScanResult>& msg, common::MacAddr bssid) {
    wlan_mlme::BSSDescription bss;
    msg.body()->bss.Clone(&bss);

    EXPECT_EQ(0, std::memcmp(bssid.byte, bss.bssid.data(), 6));
    EXPECT_EQ(bss.ssid.size(), static_cast<size_t>(9));

    const uint8_t ssid[] = {'t', 'e', 's', 't', ' ', 's', 's', 'i', 'd'};
    EXPECT_EQ(0, std::memcmp(ssid, bss.ssid.data(), sizeof(ssid)));
    EXPECT_EQ(wlan_mlme::BSSTypes::INFRASTRUCTURE, bss.bss_type);
    EXPECT_EQ(100u, bss.beacon_period);
    EXPECT_EQ(1024u, bss.timestamp);
    // Not checking for channel since DSSS Param Set IE is missing from sample
    // beacon
    EXPECT_EQ(-75, bss.rssi_dbm);
    EXPECT_EQ(WLAN_RCPI_DBMH_INVALID, bss.rcpi_dbmh);
    EXPECT_EQ(30, bss.rsni_dbh);
  }

  void AssertScanEnd(wlan_mlme::ScanResultCodes expected_code) {
    auto scan_ends = mock_dev_.GetServiceMsgs<wlan_mlme::ScanEnd>(
        fuchsia::wlan::mlme::internal::kMLME_OnScanEnd_GenOrdinal);
    ASSERT_EQ(scan_ends.size(), 1ULL);
    EXPECT_EQ(123u, scan_ends[0].body()->txn_id);
    EXPECT_EQ(expected_code, scan_ends[0].body()->code);
  }

  MockDevice mock_dev_;
  MockOnChannelHandler on_channel_handler_;
  TimerManager<TimeoutTarget> timer_mgr_;
  ChannelScheduler chan_sched_;
  Scanner scanner_;
};

wlan_mlme::ScanRequest FakeScanRequest() {
  wlan_mlme::ScanRequest req{};
  req.txn_id = 123;
  req.scan_type = wlan_mlme::ScanTypes::PASSIVE;
  req.channel_list.emplace({1});
  req.max_channel_time = 1u;
  req.ssid.resize(0);
  std::memcpy(req.bssid.data(), kBssid1, 6);
  req.probe_delay = 0;
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
  req.channel_list.emplace();

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

  ASSERT_TRUE(req.channel_list.has_value());
  EXPECT_EQ(req.channel_list->size(), 1u);
  req.channel_list->push_back(2);

  EXPECT_EQ(11u, mock_dev_.GetChannelNumber());

  ASSERT_EQ(ZX_OK, Start(std::move(req)));
  ASSERT_EQ(1u, scanner_.ScanChannel().primary);

  EXPECT_EQ(1u, mock_dev_.GetChannelNumber());

  mock_dev_.AdvanceTime(WLAN_TU(req.max_channel_time));
  chan_sched_.HandleTimeout();
  EXPECT_EQ(2u, scanner_.ScanChannel().primary);

  EXPECT_EQ(2u, mock_dev_.GetChannelNumber());
}

TEST_F(ScannerTest, PassiveScanning) {
  ASSERT_EQ(ZX_OK, Start(FakeScanRequest()));

  // Verify that no ProbeRequest was sent
  EXPECT_TRUE(mock_dev_.wlan_queue.empty());

  // Mock receiving a beacon during scan. Verify that scan result is
  // constructed.
  auto packet = CreatePacket(kBeacon);

  chan_sched_.HandleIncomingFrame(std::move(packet));
  chan_sched_.HandleTimeout();

  auto results = mock_dev_.GetServiceMsgs<wlan_mlme::ScanResult>(
      fuchsia::wlan::mlme::internal::kMLME_OnScanResult_GenOrdinal);
  ASSERT_EQ(results.size(), 1ULL);
  common::MacAddr frame_bssid({0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
  AssertScanResult(results[0], frame_bssid);

  AssertScanEnd(wlan_mlme::ScanResultCodes::SUCCESS);
}

TEST_F(ScannerTest, ActiveScanning) {
  auto req = FakeScanRequest();
  req.scan_type = wlan_mlme::ScanTypes::ACTIVE;

  ASSERT_EQ(ZX_OK, Start(std::move(req)));

  // Verify that a probe request gets sent
  ASSERT_EQ(mock_dev_.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*mock_dev_.wlan_queue.begin());
  auto frame = TypeCheckWlanFrame<MgmtFrameView<ProbeRequest>>(pkt.pkt.get());

  constexpr uint8_t expected[] = {
      // Management header
      0b01000000, 0b0,                     // frame control
      0x00, 0x00,                          // duration
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // addr1
      0x94, 0x3c, 0x49, 0x49, 0x9f, 0x2d,  // addr2 (client address)
      0xb7, 0xcd, 0x3f, 0xb0, 0x93, 0x01,  // addr3 (bssid)
      0x10, 0x00,                          // sequence control
      // Probe request body
      0x00, 0x00,                           // ssid IE
      0x01, 0x06, 12, 24, 48, 54, 96, 108,  // supported rates IE
  };
  EXPECT_RANGES_EQ(fbl::Span<const uint8_t>(frame.data(), frame.len()), expected);

  // Mock receiving a probe response during scan. Verify that scan result is
  // constructed.
  auto packet = CreatePacket(kProbeResponse);

  chan_sched_.HandleIncomingFrame(std::move(packet));
  chan_sched_.HandleTimeout();

  auto results = mock_dev_.GetServiceMsgs<wlan_mlme::ScanResult>(
      fuchsia::wlan::mlme::internal::kMLME_OnScanResult_GenOrdinal);
  ASSERT_EQ(results.size(), 1ULL);
  common::MacAddr frame_bssid({0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
  AssertScanResult(results[0], frame_bssid);

  AssertScanEnd(wlan_mlme::ScanResultCodes::SUCCESS);
}

// Main objective of this test is to verify that if we receive a probe response
// from an AP and then a beacon from the same AP that blanks out the SSID (as
// can happen in hidden AP), we keep the SSID in the scan result.
TEST_F(ScannerTest, BeaconFromHiddenAp) {
  auto req = FakeScanRequest();
  req.scan_type = wlan_mlme::ScanTypes::ACTIVE;

  ASSERT_EQ(ZX_OK, Start(std::move(req)));

  // Mock receiving a probe response and then a beacon during scan.
  auto probe_resp_pkt = CreatePacket(kProbeResponse);
  auto beacon_pkt = CreatePacket(kHiddenApBeacon);

  chan_sched_.HandleIncomingFrame(std::move(probe_resp_pkt));
  chan_sched_.HandleIncomingFrame(std::move(beacon_pkt));

  mock_dev_.SetTime(zx::time(1));
  chan_sched_.HandleTimeout();

  auto results = mock_dev_.GetServiceMsgs<wlan_mlme::ScanResult>(
      fuchsia::wlan::mlme::internal::kMLME_OnScanResult_GenOrdinal);
  ASSERT_EQ(results.size(), 1ULL);
  common::MacAddr frame_bssid({0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
  AssertScanResult(results[0], frame_bssid);

  AssertScanEnd(wlan_mlme::ScanResultCodes::SUCCESS);
}

TEST_F(ScannerTest, ActiveScanningWithProbeDelay) {
  auto req = FakeScanRequest();
  req.scan_type = wlan_mlme::ScanTypes::ACTIVE;
  req.probe_delay = 1;

  ASSERT_EQ(ZX_OK, Start(std::move(req)));

  // Verify that no probe request was sent
  EXPECT_TRUE(mock_dev_.wlan_queue.empty());

  scanner_.HandleTimeout();

  // Verify that a probe request gets sent
  ASSERT_EQ(mock_dev_.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*mock_dev_.wlan_queue.begin());
  TypeCheckWlanFrame<MgmtFrameView<ProbeRequest>>(pkt.pkt.get());
}

}  // namespace
}  // namespace wlan
