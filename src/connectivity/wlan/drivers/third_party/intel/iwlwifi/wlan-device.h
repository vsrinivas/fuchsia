// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The place holder for the code to interact with the MLME.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_WLAN_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_WLAN_DEVICE_H_

#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>

#include <ddk/device.h>

#include "garnet/lib/wlan/protocol/include/wlan/protocol/mac.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-eeprom-parse.h"

extern wlanmac_protocol_ops_t wlanmac_ops;
extern zx_protocol_device_t device_mac_ops;  // for testing only
extern wlanphy_impl_protocol_ops_t wlanphy_ops;

// for testing
size_t compose_band_list(const struct iwl_nvm_data* nvm_data,
                         wlan_info_band_t bands[WLAN_INFO_BAND_COUNT]);
void fill_band_infos(const struct iwl_nvm_data* nvm_data, const wlan_info_band_t* bands,
                     size_t bands_count, wlan_info_band_info_t* band_infos);

// Phy protocol helpers
zx_status_t phy_query(void* ctx, wlanphy_impl_info_t* info);
zx_status_t phy_create_iface(void* ctx, const wlanphy_impl_create_iface_req_t* req,
                             uint16_t* out_iface_id);
zx_status_t phy_start_iface(void* ctx, zx_device_t* zxdev, uint16_t idx);
zx_status_t phy_destroy_iface(void* ctx, uint16_t id);
zx_status_t phy_set_country(void* ctx, const wlanphy_country_t* country);
zx_status_t phy_get_country(void* ctx, wlanphy_country_t* out_country);

void phy_create_iface_undo(struct iwl_trans* iwl_trans, uint16_t idx);

// Mac protocol helpers
zx_status_t mac_query(void* ctx, uint32_t options, wlanmac_info_t* info);
zx_status_t mac_start(void* ctx, const wlanmac_ifc_protocol_t* ifc, zx_handle_t* out_sme_channel);
void mac_stop(void* ctx);
zx_status_t mac_queue_tx(void* ctx, uint32_t options, wlan_tx_packet_t* pkt);
zx_status_t mac_set_channel(void* ctx, uint32_t options, const wlan_channel_t* chan);
zx_status_t mac_configure_bss(void* ctx, uint32_t options, const wlan_bss_config_t* config);
zx_status_t mac_enable_beaconing(void* ctx, uint32_t options, const wlan_bcn_config_t* bcn_cfg);
zx_status_t mac_configure_beacon(void* ctx, uint32_t options, const wlan_tx_packet_t* pkt);
zx_status_t mac_set_key(void* ctx, uint32_t options, const wlan_key_config_t* key_config);
zx_status_t mac_configure_assoc(void* ctx, uint32_t options, const wlan_assoc_ctx_t* assoc_ctx);
zx_status_t mac_clear_assoc(void* ctx, uint32_t options, const uint8_t* peer_addr,
                            size_t peer_addr_size);
zx_status_t mac_start_hw_scan(void* ctx, const wlan_hw_scan_config_t* scan_config);
void mac_unbind(void* ctx);
void mac_release(void* ctx);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_WLAN_DEVICE_H_
