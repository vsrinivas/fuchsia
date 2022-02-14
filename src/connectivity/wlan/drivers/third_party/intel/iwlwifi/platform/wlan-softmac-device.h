// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_

#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <memory>

#include <ddktl/device.h>

struct iwl_mvm_vif;
struct iwl_trans;

namespace wlan::iwlwifi {

class MvmSta;
class WlanSoftmacDevice;

class WlanSoftmacDevice
    : public ddk::Device<WlanSoftmacDevice, ddk::Initializable, ddk::Unbindable>,
      public ::ddk::WlanSoftmacProtocol<WlanSoftmacDevice, ::ddk::base_protocol> {
 public:
  WlanSoftmacDevice(zx_device* parent, iwl_trans* drvdata, uint16_t iface_id,
                    struct iwl_mvm_vif* mvmvif);
  ~WlanSoftmacDevice();

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // WlanSoftmac interface implementation.
  zx_status_t WlanSoftmacQuery(wlan_softmac_info_t* out_info);
  zx_status_t WlanSoftmacStart(const wlan_softmac_ifc_protocol_t* ifc,
                               zx::channel* out_mlme_channel);
  void WlanSoftmacStop();
  zx_status_t WlanSoftmacQueueTx(const wlan_tx_packet_t* packet, bool* out_enqueue_pending);
  zx_status_t WlanSoftmacSetChannel(const wlan_channel_t* channel);
  zx_status_t WlanSoftmacConfigureBss(const bss_config_t* config);
  zx_status_t WlanSoftmacEnableBeaconing(const wlan_bcn_config_t* bcn_cfg);
  zx_status_t WlanSoftmacConfigureBeacon(const wlan_tx_packet_t* pkt);
  zx_status_t WlanSoftmacSetKey(const wlan_key_config_t* key_config);
  zx_status_t WlanSoftmacConfigureAssoc(const wlan_assoc_ctx_t* assoc_ctx);
  zx_status_t WlanSoftmacClearAssoc(
      const uint8_t peer_addr_list[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]);
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

  // Each peer on this interface will require a MvmSta instance.  For now, as we only support client
  // mode, we have only one peer (the AP), which simplifies things.
  std::unique_ptr<MvmSta> ap_mvm_sta_;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLAN_SOFTMAC_DEVICE_H_
