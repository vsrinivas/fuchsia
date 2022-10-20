// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DEVICE_INTERFACE_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DEVICE_INTERFACE_H_

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <fuchsia/wlan/minstrel/cpp/fidl.h>
#include <zircon/types.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/mac_frame.h>

#include "src/connectivity/wlan/lib/mlme/rust/c-binding/bindings.h"

namespace wlan {

class Packet;

// DeviceState represents the common runtime state of a device needed for
// interacting with external systems.
class DeviceState : public fbl::RefCounted<DeviceState> {
 public:
  const common::MacAddr& address() const { return addr_; }
  void set_address(const common::MacAddr& addr) { addr_ = addr; }

  wlan_channel_t channel() const { return chan_; }
  void set_channel(const wlan_channel_t& channel) { chan_ = channel; }

  bool online() { return online_; }
  void set_online(bool online) { online_ = online; }

 private:
  common::MacAddr addr_;
  wlan_channel_t chan_ = {};
  bool online_ = false;
};

// DeviceInterface represents the actions that may interact with external
// systems.
class DeviceInterface {
 public:
  virtual ~DeviceInterface() {}

  virtual zx_status_t Start(const rust_wlan_softmac_ifc_protocol_copy_t* ifc,
                            zx::channel* out_sme_channel) = 0;

  virtual zx_status_t DeliverEthernet(cpp20::span<const uint8_t> eth_frame) = 0;
  virtual zx_status_t QueueTx(std::unique_ptr<Packet> packet, wlan_tx_info_t tx_info) = 0;

  virtual zx_status_t SetChannel(wlan_channel_t channel) = 0;
  virtual zx_status_t SetStatus(uint32_t status) = 0;
  virtual zx_status_t ConfigureBss(bss_config_t* cfg) = 0;
  virtual zx_status_t EnableBeaconing(wlan_bcn_config_t* bcn_cfg) = 0;
  virtual zx_status_t ConfigureBeacon(std::unique_ptr<Packet> packet) = 0;
  virtual zx_status_t SetKey(wlan_key_config_t* key_config) = 0;
  virtual zx_status_t StartPassiveScan(const wlan_softmac_passive_scan_args_t* passive_scan_args,
                                       uint64_t* out_scan_id) = 0;
  virtual zx_status_t StartActiveScan(const wlan_softmac_active_scan_args_t* active_scan_args,
                                      uint64_t* out_scan_id) = 0;
  virtual zx_status_t CancelScan(uint64_t scan_id) = 0;
  virtual zx_status_t ConfigureAssoc(wlan_assoc_ctx_t* assoc_ctx) = 0;
  virtual zx_status_t ClearAssoc(const uint8_t[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]) = 0;

  virtual fbl::RefPtr<DeviceState> GetState() = 0;
  virtual const wlan_softmac_info_t& GetWlanSoftmacInfo() const = 0;
  virtual const discovery_support_t& GetDiscoverySupport() const = 0;
  virtual const mac_sublayer_support_t& GetMacSublayerSupport() const = 0;
  virtual const security_support_t& GetSecuritySupport() const = 0;
  virtual const spectrum_management_support_t& GetSpectrumManagementSupport() const = 0;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DEVICE_INTERFACE_H_
