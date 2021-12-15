// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_

struct iwl_mvm_vif;
struct iwl_trans;

namespace wlan::iwlwifi {

class WlanSoftmacDevice;
using WlanSoftmacDeviceType = ddk::Device<WlanSoftmacDevice, ddk::Initializable, ddk::Unbindable>;

class WlanSoftmacDevice
    : public WlanSoftmacDeviceType,
      public ::ddk::WlanSoftmacProtocol<WlanSoftmacDevice, ::ddk::base_protocol> {
 public:
  WlanSoftmacDevice(zx_device* parent, iwl_trans* drvdata, uint16_t iface_id,
                    struct iwl_mvm_vif* mvmvif)
      : WlanSoftmacDeviceType(parent), mvmvif_(mvmvif), drvdata_(drvdata), iface_id_(iface_id) {}
  ~WlanSoftmacDevice() = default;

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // WlanSoftmac interface implementation.
  zx_status_t WlanSoftmacQuery(uint32_t options, wlan_softmac_info_t* out_info);
  zx_status_t WlanSoftmacStart(const wlan_softmac_ifc_protocol_t* ifc,
                               zx::channel* out_mlme_channel);
  void WlanSoftmacStop();
  zx_status_t WlanSoftmacQueueTx(uint32_t options, const wlan_tx_packet_t* packet);
  zx_status_t WlanSoftmacSetChannel(uint32_t options, const wlan_channel_t* channel);
  zx_status_t WlanSoftmacConfigureBss(uint32_t options, const bss_config_t* config);
  zx_status_t WlanSoftmacEnableBeaconing(uint32_t options, const wlan_bcn_config_t* bcn_cfg);
  zx_status_t WlanSoftmacConfigureBeacon(uint32_t options, const wlan_tx_packet_t* pkt);
  zx_status_t WlanSoftmacSetKey(uint32_t options, const wlan_key_config_t* key_config);
  zx_status_t WlanSoftmacConfigureAssoc(uint32_t options, const wlan_assoc_ctx_t* assoc_ctx);
  zx_status_t WlanSoftmacClearAssoc(
      uint32_t options, const uint8_t peer_addr_list[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]);
  zx_status_t WlanSoftmacStartPassiveScan(const wlan_softmac_passive_scan_args_t* passive_scan_args,
                                          uint64_t* out_scan_id);
  zx_status_t WlanSoftmacStartActiveScan(const wlan_softmac_active_scan_args_t* active_scan_args,
                                         uint64_t* out_scan_id);
  zx_status_t WlanSoftmacUpdateWmmParams(wlan_ac_t ac, const wlan_wmm_params_t* params);

 protected:
  struct iwl_mvm_vif* mvmvif_;

 private:
  iwl_trans* drvdata_;
  uint16_t iface_id_;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_
