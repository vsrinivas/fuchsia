// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mac-device.h"

#include <zircon/status.h>

#include <memory>

#include <fbl/alloc_checker.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"

namespace wlan::iwlwifi {

zx_status_t MacDevice::WlanmacQuery(uint32_t options, wlanmac_info_t* out_info) {
  return mac_query(mvmvif_, options, out_info);
}

zx_status_t MacDevice::WlanmacStart(const wlanmac_ifc_protocol_t* ifc,
                                    zx::channel* out_mlme_channel) {
  return mac_start(mvmvif_, ifc, (zx_handle_t*)out_mlme_channel);
}

void MacDevice::WlanmacStop() { mac_stop(mvmvif_); }

zx_status_t MacDevice::WlanmacQueueTx(uint32_t options, wlan_tx_packet_t* pkt) {
  return mac_queue_tx(mvmvif_, options, pkt);
}

zx_status_t MacDevice::WlanmacSetChannel(uint32_t options, const wlan_channel_t* chan) {
  return mac_set_channel(mvmvif_, options, chan);
}

zx_status_t MacDevice::WlanmacConfigureBss(uint32_t options, const wlan_bss_config_t* config) {
  return mac_configure_bss(mvmvif_, options, config);
}

zx_status_t MacDevice::WlanmacEnableBeaconing(uint32_t options, const wlan_bcn_config_t* bcn_cfg) {
  return mac_enable_beaconing(mvmvif_, options, bcn_cfg);
}

zx_status_t MacDevice::WlanmacConfigureBeacon(uint32_t options, const wlan_tx_packet_t* pkt) {
  return mac_configure_beacon(mvmvif_, options, pkt);
}

zx_status_t MacDevice::WlanmacSetKey(uint32_t options, const wlan_key_config_t* key_config) {
  return mac_set_key(mvmvif_, options, key_config);
}

zx_status_t MacDevice::WlanmacConfigureAssoc(uint32_t options, const wlan_assoc_ctx_t* assoc_ctx) {
  return mac_configure_assoc(mvmvif_, options, assoc_ctx);
}

zx_status_t MacDevice::WlanmacClearAssoc(uint32_t options, const uint8_t* peer_addr_list,
                                         size_t peer_addr_count) {
  return mac_clear_assoc(mvmvif_, options, peer_addr_list, peer_addr_count);
}

zx_status_t MacDevice::WlanmacStartHwScan(const wlan_hw_scan_config_t* scan_config) {
  return mac_start_hw_scan(mvmvif_, scan_config);
}

zx_status_t MacDevice::WlanmacUpdateWmmParams(wlan_ac_t ac, const wlan_wmm_params_t* params) {
  IWL_ERR(this, "%s() needs porting\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

void MacDevice::DdkInit(ddk::InitTxn txn) {
  txn.Reply(mac_init(mvmvif_, drvdata_, zxdev(), iface_id_));
}

void MacDevice::DdkRelease() {
  IWL_DEBUG_INFO(this, "Releasing iwlwifi mac-device\n");
  mac_release(mvmvif_);

  delete this;
}

void MacDevice::DdkUnbind(ddk::UnbindTxn txn) {
  IWL_DEBUG_INFO(this, "Unbinding iwlwifi mac-device\n");
  mac_unbind(mvmvif_);
  txn.Reply();
}

}  // namespace wlan::iwlwifi
