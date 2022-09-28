// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The helper function to build a WLAN packet in 'wlan_tx_packet_t'.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_WLAN_PKT_BUILDER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_WLAN_PKT_BUILDER_H_

#include <fidl/fuchsia.wlan.softmac/cpp/driver/wire.h>
#include <stdint.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"

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
    explicit WlanPkt(const uint8_t* buf, size_t len);
    ~WlanPkt();

    WlanPkt(const WlanPkt&) = delete;             // copy constructor
    WlanPkt(WlanPkt&&) = delete;                  // move constructor
    WlanPkt& operator=(const WlanPkt&) = delete;  // copy assignment
    WlanPkt& operator=(WlanPkt&&) = delete;       // move assignment

    ieee80211_mac_packet* mac_pkt();
    const ieee80211_mac_packet* mac_pkt() const;
    ::fuchsia_wlan_softmac::wire::WlanTxPacket wlan_pkt();
    const ::fuchsia_wlan_softmac::wire::WlanTxPacket wlan_pkt() const;
    size_t len() const;

   private:
    std::unique_ptr<ieee80211_mac_packet> mac_pkt_;
    std::unique_ptr<::fuchsia_wlan_softmac::wire::WlanTxPacket> wlan_pkt_;
    std::shared_ptr<uint8_t[]> buf_;
    size_t len_;
  };

  WlanPktBuilder();
  ~WlanPktBuilder();

  WlanPktBuilder(const WlanPktBuilder&) = delete;             // copy constructor
  WlanPktBuilder(WlanPktBuilder&&) = delete;                  // move constructor
  WlanPktBuilder& operator=(const WlanPktBuilder&) = delete;  // copy assignment
  WlanPktBuilder& operator=(WlanPktBuilder&&) = delete;       // move assignment

  std::shared_ptr<WlanPkt> build(uint16_t fc = 0x0801);
  std::shared_ptr<WlanPkt> build_oversize(uint16_t fc = 0x0801);
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_WLAN_PKT_BUILDER_H_
