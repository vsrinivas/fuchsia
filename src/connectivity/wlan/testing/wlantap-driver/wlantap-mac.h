// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_WLANTAP_MAC_H_
#define SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_WLANTAP_MAC_H_

#include <ddk/device.h>
#include <fuchsia/wlan/tap/cpp/fidl.h>
#include <wlan/protocol/mac.h>

namespace wlan {

class WlantapMac {
 public:
  class Listener {
   public:
    virtual void WlantapMacStart(uint16_t id) = 0;
    virtual void WlantapMacStop(uint16_t id) = 0;
    virtual void WlantapMacQueueTx(uint16_t id, wlan_tx_packet_t* pkt) = 0;
    virtual void WlantapMacSetChannel(uint16_t id, const wlan_channel_t* channel) = 0;
    virtual void WlantapMacConfigureBss(uint16_t id, const wlan_bss_config_t* config) = 0;
    virtual void WlantapMacSetKey(uint16_t id, const wlan_key_config_t* key_config) = 0;
  };

  virtual void Rx(const std::vector<uint8_t>& data,
                  const ::fuchsia::wlan::tap::WlanRxInfo& rx_info) = 0;
  virtual void Status(uint32_t status) = 0;

  virtual void ReportTxStatus(const ::fuchsia::wlan::tap::WlanTxStatus& ts) = 0;

  virtual void RemoveDevice() = 0;

  virtual ~WlantapMac() = default;
};

zx_status_t CreateWlantapMac(zx_device_t* parent_phy, const ::fuchsia::wlan::device::MacRole role,
                             const ::fuchsia::wlan::tap::WlantapPhyConfig* phy_config, uint16_t id,
                             WlantapMac::Listener* listener, zx::channel sme_channel,
                             WlantapMac** ret);

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_WLANTAP_MAC_H_
