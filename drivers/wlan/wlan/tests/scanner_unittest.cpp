// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scanner.h"

#include "clock.h"
#include "mac_frame.h"
#include "packet.h"

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <cstring>
#include <gtest/gtest.h>
#include <mxtl/unique_ptr.h>

namespace wlan {
namespace {

const uint8_t kBeacon[] = {
    0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x00, 0x00, 0x09, 0x74, 0x65, 0x73, 0x74, 0x20, 0x73, 0x73, 0x69, 0x64,
};

class ScannerTest : public ::testing::Test {
  public:
    ScannerTest()
      : scanner_(&clock_), buffer_alloc_(1, true) {
        SetupMessages();
    }

  protected:
    void SetupMessages() {
        req_ = ScanRequest::New();
        req_->channel_list.push_back(1);
        resp_ = ScanResponse::New();
    }

    void SetPassive() {
        req_->scan_type = ScanTypes::PASSIVE;
    }

    void SetActive() {
        req_->scan_type = ScanTypes::ACTIVE;
    }

    mx_status_t Start() {
        return scanner_.Start(req_.Clone(), std::move(resp_));
    }

    ScanRequestPtr req_;
    ScanResponsePtr resp_;
    TestClock clock_;
    Scanner scanner_;
    mxtl::SlabAllocator<BufferAllocatorTraits> buffer_alloc_;
};

TEST_F(ScannerTest, Start) {
    EXPECT_FALSE(scanner_.IsRunning());
    EXPECT_EQ(MX_OK, Start());
    EXPECT_TRUE(scanner_.IsRunning());
}

TEST_F(ScannerTest, Start_InvalidChannelTimes) {
    req_->min_channel_time = 2;
    req_->max_channel_time = 1;
    EXPECT_EQ(MX_ERR_INVALID_ARGS, Start());
    EXPECT_FALSE(scanner_.IsRunning());
}

TEST_F(ScannerTest, Start_NoChannels) {
    SetupMessages();
    req_->channel_list.resize(0);
    EXPECT_EQ(MX_ERR_INVALID_ARGS, Start());
    EXPECT_FALSE(scanner_.IsRunning());
}

TEST_F(ScannerTest, Reset) {
    ASSERT_EQ(MX_OK, Start());
    ASSERT_TRUE(scanner_.IsRunning());

    scanner_.Reset();
    EXPECT_FALSE(scanner_.IsRunning());
}

TEST_F(ScannerTest, PassiveScan) {
    SetPassive();

    ASSERT_EQ(MX_OK, Start());
    EXPECT_EQ(Scanner::Type::kPassive, scanner_.ScanType());
}

TEST_F(ScannerTest, ActiveScan) {
    SetActive();

    ASSERT_EQ(MX_OK, Start());
    EXPECT_EQ(Scanner::Type::kActive, scanner_.ScanType());
}

TEST_F(ScannerTest, ScanChannel) {
    ASSERT_EQ(MX_OK, Start());
    auto chan = scanner_.ScanChannel();
    EXPECT_EQ(1u, chan.channel_num);
}

TEST_F(ScannerTest, Timeout_MinChannelTime) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;

    ASSERT_EQ(MX_OK, Start());
    EXPECT_EQ(WLAN_TU(req_->min_channel_time), scanner_.NextTimeout());

    clock_.Set(WLAN_TU(req_->min_channel_time));
    EXPECT_EQ(Scanner::Status::kContinueScan, scanner_.HandleTimeout(clock_.Now()));
    EXPECT_EQ(WLAN_TU(req_->max_channel_time), scanner_.NextTimeout());
}

TEST_F(ScannerTest, Timeout_MaxChannelTime) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;

    ASSERT_EQ(MX_OK, Start());

    clock_.Set(WLAN_TU(req_->min_channel_time));
    ASSERT_EQ(Scanner::Status::kContinueScan, scanner_.HandleTimeout(clock_.Now()));

    clock_.Set(WLAN_TU(req_->max_channel_time));
    EXPECT_EQ(Scanner::Status::kFinishScan, scanner_.HandleTimeout(clock_.Now()));
}

TEST_F(ScannerTest, Timeout_NextChannel) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;
    req_->channel_list.push_back(2);

    ASSERT_EQ(MX_OK, Start());
    ASSERT_EQ(1u, scanner_.ScanChannel().channel_num);

    clock_.Set(WLAN_TU(req_->min_channel_time));
    ASSERT_EQ(Scanner::Status::kContinueScan, scanner_.HandleTimeout(clock_.Now()));

    clock_.Set(WLAN_TU(req_->max_channel_time));
    EXPECT_EQ(Scanner::Status::kNextChannel, scanner_.HandleTimeout(clock_.Now()));
    EXPECT_EQ(2u, scanner_.ScanChannel().channel_num);
    EXPECT_EQ(clock_.Now() + WLAN_TU(req_->min_channel_time), scanner_.NextTimeout());
}

TEST_F(ScannerTest, Timeout_ProbeDelay) {
    SetActive();
    req_->probe_delay = 1;
    req_->min_channel_time = 5;
    req_->max_channel_time = 10;

    ASSERT_EQ(MX_OK, Start());
    EXPECT_EQ(WLAN_TU(req_->probe_delay), scanner_.NextTimeout());

    clock_.Set(WLAN_TU(req_->probe_delay));
    EXPECT_EQ(Scanner::Status::kStartActiveScan, scanner_.HandleTimeout(clock_.Now()));
    EXPECT_EQ(WLAN_TU(req_->min_channel_time), scanner_.NextTimeout());
}

TEST_F(ScannerTest, ScanResponse) {
    SetPassive();

    ASSERT_EQ(MX_OK, Start());
    auto buf = buffer_alloc_.New();
    ASSERT_NE(nullptr, buf);

    Packet p(std::move(buf), sizeof(kBeacon));
    p.CopyFrom(kBeacon, sizeof(kBeacon), 0);
    wlan_rx_info_t info;
    info.flags = WLAN_RX_INFO_RSSI_PRESENT | WLAN_RX_INFO_SNR_PRESENT;
    info.chan = { 1 };
    info.rssi = 10;
    info.snr = 60;
    p.CopyCtrlFrom(info);

    EXPECT_EQ(Scanner::Status::kContinueScan, scanner_.HandleBeacon(&p));
    clock_.Set(1);
    EXPECT_EQ(Scanner::Status::kFinishScan, scanner_.HandleTimeout(clock_.Now()));

    auto resp = scanner_.ScanResults();
    ASSERT_NE(nullptr, resp.get());
    ASSERT_EQ(1u, resp->bss_description_set.size());
    EXPECT_EQ(ResultCodes::SUCCESS, resp->result_code);

    auto bss = resp->bss_description_set[0].get();
    EXPECT_EQ(0, std::memcmp(kBeacon + 16, bss->bssid.data(), 6));
    EXPECT_STREQ("test ssid", bss->ssid.get().c_str());
    EXPECT_EQ(BSSTypes::INFRASTRUCTURE, bss->bss_type);
    EXPECT_EQ(100u, bss->beacon_period);
    EXPECT_EQ(1024u, bss->timestamp);
    EXPECT_EQ(1u, bss->channel);
    EXPECT_EQ(10u, bss->rssi_measurement);
    EXPECT_EQ(0xff, bss->rcpi_measurement);
    EXPECT_EQ(60u, bss->rsni_measurement);
}

}  // namespace
}  // namespace wlan
