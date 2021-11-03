// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/mac/cpp/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLANMAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLANMAC_DEVICE_H_

struct iwl_mvm_vif;
struct iwl_trans;

namespace wlan::iwlwifi {

class WlanmacDevice;
using WlanmacDeviceType = ddk::Device<WlanmacDevice, ddk::Initializable, ddk::Unbindable>;

class WlanmacDevice : public WlanmacDeviceType,
                      public ::ddk::WlanmacProtocol<WlanmacDevice, ::ddk::base_protocol> {
 public:
  WlanmacDevice(zx_device* parent, iwl_trans* drvdata, uint16_t iface_id,
                struct iwl_mvm_vif* mvmvif)
      : WlanmacDeviceType(parent), mvmvif_(mvmvif), drvdata_(drvdata), iface_id_(iface_id) {}
  ~WlanmacDevice() = default;

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // Wlanmac interface implementation.
  zx_status_t WlanmacQuery(uint32_t options, wlanmac_info_t* out_info);
  zx_status_t WlanmacStart(const wlanmac_ifc_protocol_t* ifc, zx::channel* out_mlme_channel);
  void WlanmacStop();
  zx_status_t WlanmacQueueTx(uint32_t options, const wlan_tx_packet_t* packet);
  zx_status_t WlanmacSetChannel(uint32_t options, const wlan_channel_t* channel);
  zx_status_t WlanmacConfigureBss(uint32_t options, const bss_config_t* config);
  zx_status_t WlanmacEnableBeaconing(uint32_t options, const wlan_bcn_config_t* bcn_cfg);
  zx_status_t WlanmacConfigureBeacon(uint32_t options, const wlan_tx_packet_t* pkt);
  zx_status_t WlanmacSetKey(uint32_t options, const wlan_key_config_t* key_config);
  zx_status_t WlanmacConfigureAssoc(uint32_t options, const wlan_assoc_ctx_t* assoc_ctx);
  zx_status_t WlanmacClearAssoc(uint32_t options,
                                const uint8_t peer_addr_list[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]);
  zx_status_t WlanmacStartHwScan(const wlan_hw_scan_config_t* scan_config);
  zx_status_t WlanmacUpdateWmmParams(wlan_ac_t ac, const wlan_wmm_params_t* params);

 protected:
  struct iwl_mvm_vif* mvmvif_;

 private:
  iwl_trans* drvdata_;
  uint16_t iface_id_;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLANMAC_DEVICE_H_
