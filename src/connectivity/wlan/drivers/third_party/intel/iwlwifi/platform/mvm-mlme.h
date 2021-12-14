// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the interface between the iwlwifi MVM opmode and the Fuchsia MLME.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_MLME_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_MLME_H_

#include <fuchsia/hardware/wlan/phyinfo/cpp/banjo.h>
#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/cpp/banjo.h>
#include <fuchsia/wlan/common/cpp/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/banjo.h>
#include <lib/ddk/device.h>

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

// IEEE Std 802.11-2016, Table 9-19
#define WLAN_MSDU_MAX_LEN 2304UL

// A reasonable key length is <= 256 bits.
#define WLAN_MAX_KEY_LEN ((size_t)(256 / 8))

extern wlanmac_protocol_ops_t wlanmac_ops;
extern zx_protocol_device_t device_mac_ops;  // for testing only

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
zx_status_t mac_start(void* ctx, const wlanmac_ifc_protocol_t* ifc, zx_handle_t* out_mlme_channel);
void mac_stop(void* ctx);
zx_status_t mac_queue_tx(void* ctx, uint32_t options, const wlan_tx_packet_t* packet);
zx_status_t mac_set_channel(void* ctx, uint32_t options, const wlan_channel_t* channel);
zx_status_t mac_configure_bss(void* ctx, uint32_t options, const bss_config_t* config);
zx_status_t mac_enable_beaconing(void* ctx, uint32_t options, const wlan_bcn_config_t* bcn_cfg);
zx_status_t mac_configure_beacon(void* ctx, uint32_t options,
                                 const wlan_tx_packet_t* packet_template);
zx_status_t mac_set_key(void* ctx, uint32_t options, const wlan_key_config_t* key_config);
zx_status_t mac_configure_assoc(void* ctx, uint32_t options, const wlan_assoc_ctx_t* assoc_ctx);
zx_status_t mac_clear_assoc(void* ctx, uint32_t options,
                            const uint8_t peer_addr[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]);
zx_status_t mac_start_passive_scan(void* ctx, const wlanmac_passive_scan_args_t* passive_scan_args,
                                   uint64_t* out_scan_id);
zx_status_t mac_start_active_scan(void* ctx, const wlanmac_active_scan_args_t* active_scan_args,
                                  uint64_t* out_scan_id);
zx_status_t mac_init(void* ctx, struct iwl_trans* drvdata, zx_device_t* zxdev, uint16_t idx);
void mac_unbind(void* ctx);
void mac_release(void* ctx);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_MLME_H_
