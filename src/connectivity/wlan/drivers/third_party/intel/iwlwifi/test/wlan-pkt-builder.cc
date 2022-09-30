// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/wlan-pkt-builder.h"

#include <cstring>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/common.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"

namespace wlan::testing {

WlanPktBuilder::WlanPkt::WlanPkt(const uint8_t* buf, size_t len)
    : mac_pkt_(std::make_unique<ieee80211_mac_packet>()),
      wlan_pkt_(std::make_unique<::fuchsia_wlan_softmac::wire::WlanTxPacket>()),
      buf_(new uint8_t[len]),
      len_(len) {
  ASSERT_NOT_NULL(mac_pkt_);
  ASSERT_NOT_NULL(wlan_pkt_);
  ASSERT_NOT_NULL(buf_);
  std::memcpy(&*buf_, buf, len);

  *mac_pkt_ = {};
  mac_pkt_->common_header = reinterpret_cast<ieee80211_frame_header*>(&*buf_);
  mac_pkt_->header_size = ieee80211_get_header_len(mac_pkt_->common_header);
  mac_pkt_->body = &*buf_ + mac_pkt_->header_size;
  mac_pkt_->body_size = len - mac_pkt_->header_size;

  *wlan_pkt_ = {};
  wlan_pkt_->mac_frame = fidl::VectorView<uint8_t>::FromExternal(buf_.get(), len);
  wlan_pkt_->info.tx_flags = 0;
  wlan_pkt_->info.channel_bandwidth = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw20;
  wlan_pkt_->info.phy = fuchsia_wlan_common::WlanPhyType::kDsss;
}

WlanPktBuilder::WlanPkt::~WlanPkt() = default;

ieee80211_mac_packet* WlanPktBuilder::WlanPkt::mac_pkt() { return mac_pkt_.get(); }

const ieee80211_mac_packet* WlanPktBuilder::WlanPkt::mac_pkt() const { return mac_pkt_.get(); }

::fuchsia_wlan_softmac::wire::WlanTxPacket WlanPktBuilder::WlanPkt::wlan_pkt() {
  return *wlan_pkt_;
}

const ::fuchsia_wlan_softmac::wire::WlanTxPacket WlanPktBuilder::WlanPkt::wlan_pkt() const {
  return *wlan_pkt_;
}

size_t WlanPktBuilder::WlanPkt::len() const { return len_; }

WlanPktBuilder::WlanPktBuilder() = default;

WlanPktBuilder::~WlanPktBuilder() = default;

std::shared_ptr<WlanPktBuilder::WlanPkt> WlanPktBuilder::build(uint16_t fc) {
  const uint8_t fc0 = (uint8_t)fc & 0xff;
  const uint8_t fc1 = (uint8_t)(fc >> 8);
  const uint8_t kMacPkt[] = {
      fc0,  fc1,                           // frame_ctrl
      0x00, 0x00,                          // duration
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // MAC1
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC2
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC3
      0x00, 0x00,                          // seq_ctrl
      0x45, 0x00, 0x55, 0x66, 0x01, 0x83,  // random IP packet...
  };

  std::shared_ptr<WlanPkt> wlan_pkt(new WlanPkt(kMacPkt, sizeof(kMacPkt)));
  ZX_ASSERT(wlan_pkt);
  return wlan_pkt;
}

std::shared_ptr<WlanPktBuilder::WlanPkt> WlanPktBuilder::build_oversize(uint16_t fc) {
  const uint8_t fc0 = (uint8_t)fc & 0xff;
  const uint8_t fc1 = (uint8_t)(fc >> 8);
  const uint8_t kMacPkt[WLAN_MSDU_MAX_LEN + 1] = {
      fc0,  fc1,                           // frame_ctrl
      0x00, 0x00,                          // duration
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // MAC1
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC2
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC3
      0x00, 0x00,                          // seq_ctrl
      // No data following the header, but the packet buffer size will be 1 byte larger than
      // WLAN_MSDU_MAX_LEN.
  };

  std::shared_ptr<WlanPkt> wlan_pkt(new WlanPkt(kMacPkt, WLAN_MSDU_MAX_LEN + 1));
  ZX_ASSERT(wlan_pkt);
  return wlan_pkt;
}

}  // namespace wlan::testing
