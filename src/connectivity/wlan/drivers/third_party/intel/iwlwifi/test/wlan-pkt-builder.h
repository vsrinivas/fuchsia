// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The helper function to build a WLAN packet in 'wlan_tx_packet_t'.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_WLAN_PKT_BUILDER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_WLAN_PKT_BUILDER_H_

#include <fuchsia/wlan/common/c/banjo.h>
#include <stdint.h>
#include <string.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace wlan::testing {

//
// This builder class is used to help test cases to generate WLAN packet.
//
// The 'build()' function will return a WlanPkt object, which is a smart pointer and will be
// released when it is no longer referred. Currently, the returned packet is just an arbitray WLAN
// packet with IP packet as its payload.
//
class WlanPktBuilder {
 public:
  //
  // The WlanPkt class is the container to manage the allocated resource.
  //
  // Call 'pkt()' to retrieve the 'wlan_tx_packet_t' object it holds.
  // Call 'len()' to get the WLAN packet length.
  //
  class WlanPkt {
   public:
    WlanPkt(const uint8_t* buf, size_t len)
        : pkt_(new wlan_tx_packet_t()), buf_(new uint8_t[len]), len_(len) {
      ASSERT_NOT_NULL(pkt_);
      ASSERT_NOT_NULL(buf_);

      std::memcpy(&*buf_, buf, len);
      pkt_->packet_head.data_buffer = &*buf_;
      pkt_->packet_head.data_size = len;

      pkt_->info.tx_flags = 0;
      pkt_->info.channel_bandwidth = CHANNEL_BANDWIDTH_CBW20;
    }
    ~WlanPkt() {}

    WlanPkt(const WlanPkt&) = delete;             // copy constructor
    WlanPkt(WlanPkt&&) = delete;                  // move constructor
    WlanPkt& operator=(const WlanPkt&) = delete;  // copy assignment
    WlanPkt& operator=(WlanPkt&&) = delete;       // move assignment

    wlan_tx_packet_t* pkt() { return &*pkt_; }
    size_t len() { return len_; }

   private:
    std::unique_ptr<wlan_tx_packet_t> pkt_;
    std::shared_ptr<uint8_t[]> buf_;
    size_t len_;
  };

  WlanPktBuilder() {}
  ~WlanPktBuilder() {}

  WlanPktBuilder(const WlanPktBuilder&) = delete;             // copy constructor
  WlanPktBuilder(WlanPktBuilder&&) = delete;                  // move constructor
  WlanPktBuilder& operator=(const WlanPktBuilder&) = delete;  // copy assignment
  WlanPktBuilder& operator=(WlanPktBuilder&&) = delete;       // move assignment

  std::shared_ptr<WlanPkt> build() {
    uint8_t mac_pkt[] = {
        0x08, 0x01,                          // frame_ctrl
        0x00, 0x00,                          // duration
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // MAC1
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC2
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC3
        0x00, 0x00,                          // seq_ctrl
        0x45, 0x00, 0x55, 0x66, 0x01, 0x83,  // random IP packet...
    };

    std::shared_ptr<WlanPkt> wlan_pkt(new WlanPkt(mac_pkt, sizeof(mac_pkt)));
    ZX_ASSERT(wlan_pkt);
    return wlan_pkt;
  }
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_WLAN_PKT_BUILDER_H_
