// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/mac/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}  // extern "C"

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MAC_DEVICE_H_

namespace wlan::iwlwifi {

class MacDevice;
using MacDeviceType = ddk::Device<MacDevice, ddk::Unbindable>;

class MacDevice : public MacDeviceType,
                  public ::ddk::WlanmacProtocol<MacDevice, ::ddk::base_protocol> {
 public:
  MacDevice(zx_device* parent) : MacDeviceType(parent){};
  ~MacDevice() = default;

  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // Wlanmac interface implementation.
  zx_status_t WlanmacQuery(uint32_t options, wlanmac_info_t* out_info);
  zx_status_t WlanmacStart(const wlanmac_ifc_protocol_t* ifc, zx::channel* out_mlme_channel);
  void WlanmacStop();
  zx_status_t WlanmacQueueTx(uint32_t options, wlan_tx_packet_t* pkt);
  zx_status_t WlanmacSetChannel(uint32_t options, const wlan_channel_t* chan);
  zx_status_t WlanmacConfigureBss(uint32_t options, const wlan_bss_config_t* config);
  zx_status_t WlanmacEnableBeaconing(uint32_t options, const wlan_bcn_config_t* bcn_cfg);
  zx_status_t WlanmacConfigureBeacon(uint32_t options, const wlan_tx_packet_t* pkt);
  zx_status_t WlanmacSetKey(uint32_t options, const wlan_key_config_t* key_config);
  zx_status_t WlanmacConfigureAssoc(uint32_t options, const wlan_assoc_ctx_t* assoc_ctx);
  zx_status_t WlanmacClearAssoc(uint32_t options, const uint8_t* peer_addr_list,
                                size_t peer_addr_count);
  zx_status_t WlanmacStartHwScan(const wlan_hw_scan_config_t* scan_config);
  zx_status_t WlanmacUpdateWmmParams(wlan_ac_t ac, const wlan_wmm_params_t* params);

  void set_mvmvif(struct iwl_mvm_vif* mvmvif) { mvmvif_ = mvmvif; };

 protected:
  struct iwl_mvm_vif* mvmvif_;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MAC_DEVICE_H_
