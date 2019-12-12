// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_STA_IFC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_STA_IFC_H_

#include <net/ethernet.h>

#include <ddk/protocol/wlan/info.h>
#include <wlan/common/macaddr.h>
#include <wlan/protocol/info.h>

namespace wlan::simulation {

class StationIfc {
 public:
  // Placeholder for eventual packet-level packet handler
  virtual void Rx(void* pkt) = 0;

  // Simplified beacon handler, eventually to be incorporated into Rx() functionality
  virtual void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                        const common::MacAddr& bssid) = 0;

  // Receive an assocation request
  virtual void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                          const common::MacAddr& bssid) = 0;

  // Receive an association response
  virtual void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                           const common::MacAddr& dst, uint16_t status) = 0;

  // Receive an disassocation request
  virtual void RxDisassocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                             const common::MacAddr& dst, uint16_t reason) = 0;

  // Receive a Probe request
  virtual void RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) = 0;

  // Receive a Probe response
  virtual void RxProbeResp(const wlan_channel_t& channel, const common::MacAddr& src,
                           const common::MacAddr& dst, const wlan_ssid_t& ssid) = 0;

  // Receive notification of a simulation event
  virtual void ReceiveNotification(void* payload) = 0;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_STA_IFC_H_
