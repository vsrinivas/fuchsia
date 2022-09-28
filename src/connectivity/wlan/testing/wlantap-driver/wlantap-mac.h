// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_WLANTAP_MAC_H_
#define SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_WLANTAP_MAC_H_

#include <fidl/fuchsia.wlan.softmac/cpp/driver/wire.h>
#include <fidl/fuchsia.wlan.tap/cpp/driver/wire.h>
#include <lib/ddk/device.h>

namespace wlan_tap = fuchsia_wlan_tap::wire;
namespace wlan_common = fuchsia_wlan_common::wire;
namespace wlan_internal = fuchsia_wlan_internal::wire;
namespace wlan_softmac = fuchsia_wlan_softmac::wire;

namespace wlan {

class WlantapMac {
 public:
  class Listener {
   public:
    virtual void WlantapMacStart(uint16_t id) = 0;
    virtual void WlantapMacStop(uint16_t id) = 0;
    virtual void WlantapMacQueueTx(uint16_t id, const wlan_softmac::WlanTxPacket& pkt) = 0;
    virtual void WlantapMacSetChannel(uint16_t id, const wlan_common::WlanChannel& channel) = 0;
    virtual void WlantapMacConfigureBss(uint16_t id, const wlan_internal::BssConfig& config) = 0;
    virtual void WlantapMacStartScan(uint16_t id, uint64_t scan_id) = 0;
    virtual void WlantapMacSetKey(uint16_t id, const wlan_softmac::WlanKeyConfig& key_config) = 0;
  };

  virtual void Rx(const fidl::VectorView<uint8_t>& data, const wlan_tap::WlanRxInfo& rx_info) = 0;
  virtual void Status(uint32_t status) = 0;

  virtual void ReportTxStatus(const wlan_common::WlanTxStatus& ts) = 0;

  virtual void ScanComplete(uint64_t scan_id, int32_t status) = 0;

  virtual void RemoveDevice() = 0;

  virtual ~WlantapMac() = default;
};

zx_status_t CreateWlantapMac(zx_device_t* parent_phy, const wlan_common::WlanMacRole role,
                             const std::shared_ptr<const wlan_tap::WlantapPhyConfig> phy_config,
                             uint16_t id, WlantapMac::Listener* listener, zx::channel sme_channel,
                             WlantapMac** ret);

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_WLANTAP_MAC_H_
