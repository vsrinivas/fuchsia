// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scanner.h"

#include "clock.h"
#include "device_interface.h"
#include "mac_frame.h"
#include "packet.h"
#include "serialize.h"
#include "timer.h"

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <gtest/gtest.h>
#include <cstring>

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

namespace wlan {
namespace {

const uint8_t kBeacon[] = {
    0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x00, 0x00, 0x09, 0x74, 0x65, 0x73, 0x74, 0x20, 0x73, 0x73, 0x69, 0x64,
};

struct MockDevice : public DeviceInterface {
   public:
    MockDevice() { state = fbl::AdoptRef(new DeviceState); }

    zx_status_t GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) override final {
        // Should not be used by Scanner at this time.
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t SendEthernet(fbl::unique_ptr<Packet> packet) override final {
        eth_queue.Enqueue(std::move(packet));
        return ZX_OK;
    }

    zx_status_t SendWlan(fbl::unique_ptr<Packet> packet) override final {
        wlan_queue.Enqueue(std::move(packet));
        return ZX_OK;
    }

    zx_status_t SendService(fbl::unique_ptr<Packet> packet) override final {
        svc_queue.Enqueue(std::move(packet));
        return ZX_OK;
    }

    zx_status_t SetChannel(wlan_channel_t chan) override final {
        state->set_channel(chan);
        return ZX_OK;
    }

    zx_status_t SetStatus(uint32_t status) override final {
        state->set_online(status == 1);
        return ZX_OK;
    }

    fbl::RefPtr<DeviceState> GetState() override final { return state; }

    fbl::RefPtr<DeviceState> state;
    PacketQueue eth_queue;
    PacketQueue wlan_queue;
    PacketQueue svc_queue;
};

class ScannerTest : public ::testing::Test {
   public:
    ScannerTest() : scanner_(&mock_dev_, fbl::unique_ptr<Timer>(new TestTimer(1u, &clock_))) {
        SetupMessages();
    }

   protected:
    void SetupMessages() {
        req_ = ScanRequest::New();
        req_->channel_list.push_back(1);
        resp_ = ScanResponse::New();
    }

    void SetPassive() { req_->scan_type = ScanTypes::PASSIVE; }

    void SetActive() { req_->scan_type = ScanTypes::ACTIVE; }

    zx_status_t Start() { return scanner_.Start(req_.Clone()); }

    uint16_t CurrentChannel() { return mock_dev_.GetState()->channel().channel_num; }

    zx_status_t DeserializeResponse() {
        EXPECT_EQ(1u, mock_dev_.svc_queue.size());
        auto packet = mock_dev_.svc_queue.Dequeue();
        return DeserializeServiceMsg(*packet, Method::SCAN_confirm, &resp_);
    }

    ScanRequestPtr req_;
    ScanResponsePtr resp_;
    TestClock clock_;
    MockDevice mock_dev_;
    Scanner scanner_;
};

TEST_F(ScannerTest, Start) {
    EXPECT_EQ(0u, CurrentChannel());
    EXPECT_FALSE(scanner_.IsRunning());

    EXPECT_EQ(ZX_OK, Start());
    EXPECT_TRUE(scanner_.IsRunning());

    EXPECT_EQ(1u, CurrentChannel());
}

TEST_F(ScannerTest, Start_InvalidChannelTimes) {
    req_->min_channel_time = 2;
    req_->max_channel_time = 1;

    EXPECT_EQ(0u, CurrentChannel());

    EXPECT_EQ(ZX_OK, Start());
    EXPECT_FALSE(scanner_.IsRunning());
    EXPECT_EQ(0u, CurrentChannel());

    EXPECT_EQ(ZX_OK, DeserializeResponse());
    EXPECT_EQ(0u, resp_->bss_description_set.size());
    EXPECT_EQ(ScanResultCodes::NOT_SUPPORTED, resp_->result_code);
}

TEST_F(ScannerTest, Start_NoChannels) {
    SetupMessages();
    req_->channel_list.resize(0);

    EXPECT_EQ(0u, CurrentChannel());

    EXPECT_EQ(ZX_OK, Start());
    EXPECT_FALSE(scanner_.IsRunning());
    EXPECT_EQ(0u, CurrentChannel());

    EXPECT_EQ(ZX_OK, DeserializeResponse());
    EXPECT_EQ(0u, resp_->bss_description_set.size());
    EXPECT_EQ(ScanResultCodes::NOT_SUPPORTED, resp_->result_code);
}

TEST_F(ScannerTest, Reset) {
    ASSERT_EQ(ZX_OK, Start());
    ASSERT_TRUE(scanner_.IsRunning());

    scanner_.Reset();
    EXPECT_FALSE(scanner_.IsRunning());
    // TODO(tkilbourn): check all the other invariants
}

TEST_F(ScannerTest, PassiveScan) {
    SetPassive();

    ASSERT_EQ(ZX_OK, Start());
    EXPECT_EQ(Scanner::Type::kPassive, scanner_.ScanType());
}

TEST_F(ScannerTest, ActiveScan) {
    SetActive();

    ASSERT_EQ(ZX_OK, Start());
    EXPECT_EQ(Scanner::Type::kActive, scanner_.ScanType());
}

TEST_F(ScannerTest, ScanChannel) {
    ASSERT_EQ(ZX_OK, Start());
    auto chan = scanner_.ScanChannel();
    EXPECT_EQ(1u, chan.channel_num);
}

TEST_F(ScannerTest, Timeout_MinChannelTime) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;

    ASSERT_EQ(ZX_OK, Start());
    EXPECT_EQ(WLAN_TU(req_->min_channel_time), scanner_.timer().deadline());

    clock_.Set(WLAN_TU(req_->min_channel_time));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());
    EXPECT_EQ(WLAN_TU(req_->max_channel_time), scanner_.timer().deadline());
}

TEST_F(ScannerTest, Timeout_MaxChannelTime) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;

    ASSERT_EQ(ZX_OK, Start());

    clock_.Set(WLAN_TU(req_->min_channel_time));
    ASSERT_EQ(ZX_OK, scanner_.HandleTimeout());

    clock_.Set(WLAN_TU(req_->max_channel_time));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());

    EXPECT_EQ(ZX_OK, DeserializeResponse());
    EXPECT_EQ(0u, resp_->bss_description_set.size());
    EXPECT_EQ(ScanResultCodes::SUCCESS, resp_->result_code);
}

TEST_F(ScannerTest, Timeout_NextChannel) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;
    req_->channel_list.push_back(2);

    EXPECT_EQ(0u, CurrentChannel());

    ASSERT_EQ(ZX_OK, Start());
    ASSERT_EQ(1u, scanner_.ScanChannel().channel_num);

    EXPECT_EQ(1u, CurrentChannel());

    clock_.Set(WLAN_TU(req_->min_channel_time));
    ASSERT_EQ(ZX_OK, scanner_.HandleTimeout());

    clock_.Set(WLAN_TU(req_->max_channel_time));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());
    EXPECT_EQ(2u, scanner_.ScanChannel().channel_num);
    EXPECT_EQ(clock_.Now() + WLAN_TU(req_->min_channel_time), scanner_.timer().deadline());

    EXPECT_EQ(2u, CurrentChannel());
}

TEST_F(ScannerTest, DISABLED_Timeout_ProbeDelay) {
    SetActive();
    req_->probe_delay = 1;
    req_->min_channel_time = 5;
    req_->max_channel_time = 10;

    ASSERT_EQ(ZX_OK, Start());
    EXPECT_EQ(WLAN_TU(req_->probe_delay), scanner_.timer().deadline());

    clock_.Set(WLAN_TU(req_->probe_delay));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());
    EXPECT_EQ(WLAN_TU(req_->min_channel_time), scanner_.timer().deadline());
}

TEST_F(ScannerTest, ScanResponse) {
    SetPassive();

    ASSERT_EQ(ZX_OK, Start());

    auto buf = LargeBufferAllocator::New();
    ASSERT_NE(nullptr, buf);

    Packet p(std::move(buf), sizeof(kBeacon));
    p.CopyFrom(kBeacon, sizeof(kBeacon), 0);
    wlan_rx_info_t info;
    info.flags = WLAN_RX_INFO_RSSI_PRESENT | WLAN_RX_INFO_SNR_PRESENT;
    info.chan = {1};
    info.rssi = 10;
    info.snr = 60;
    p.CopyCtrlFrom(info);

    EXPECT_EQ(ZX_OK, scanner_.HandleBeaconOrProbeResponse(&p));
    clock_.Set(1);
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());

    EXPECT_EQ(ZX_OK, DeserializeResponse());
    ASSERT_EQ(1u, resp_->bss_description_set.size());
    EXPECT_EQ(ScanResultCodes::SUCCESS, resp_->result_code);

    auto bss = resp_->bss_description_set[0].get();
    EXPECT_EQ(0, std::memcmp(kBeacon + 16, bss->bssid.data(), 6));
    EXPECT_STREQ("test ssid", bss->ssid.get().c_str());
    EXPECT_EQ(BSSTypes::INFRASTRUCTURE, bss->bss_type);
    EXPECT_EQ(100u, bss->beacon_period);
    EXPECT_EQ(1024u, bss->timestamp);
    // EXPECT_EQ(1u, bss->channel);  // IE missing. info.chan != bss->channel.
    EXPECT_EQ(10u, bss->rssi_measurement);
    EXPECT_EQ(0, bss->rcpi_measurement);  // Not reported. Default at 0.
    EXPECT_EQ(60u, bss->rsni_measurement);
}

// TODO(hahnr): add test for active scanning

}  // namespace
}  // namespace wlan
