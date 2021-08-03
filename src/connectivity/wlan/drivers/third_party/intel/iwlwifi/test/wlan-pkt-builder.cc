// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/wlan-pkt-builder.h"

#include <fuchsia/wlan/common/cpp/banjo.h>

#include <cstring>

#include <zxtest/zxtest.h>

namespace wlan::testing {

WlanPktBuilder::WlanPkt::WlanPkt(const uint8_t* buf, size_t len)
    : pkt_(new wlan_tx_packet_t()), buf_(new uint8_t[len]), len_(len) {
  ASSERT_NOT_NULL(pkt_);
  ASSERT_NOT_NULL(buf_);

  std::memcpy(&*buf_, buf, len);
  pkt_->packet_head.data_buffer = &*buf_;
  pkt_->packet_head.data_size = len;

  pkt_->info.tx_flags = 0;
  pkt_->info.channel_bandwidth = CHANNEL_BANDWIDTH_CBW20;
}

WlanPktBuilder::WlanPkt::~WlanPkt() = default;

wlan_tx_packet_t* WlanPktBuilder::WlanPkt::pkt() { return pkt_.get(); }

const wlan_tx_packet_t* WlanPktBuilder::WlanPkt::pkt() const { return pkt_.get(); }

size_t WlanPktBuilder::WlanPkt::len() const { return len_; }

WlanPktBuilder::WlanPktBuilder() = default;

WlanPktBuilder::~WlanPktBuilder() = default;

std::shared_ptr<WlanPktBuilder::WlanPkt> WlanPktBuilder::build() {
  static constexpr uint8_t kMacPkt[] = {
      0x08, 0x01,                          // frame_ctrl
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

}  // namespace wlan::testing
