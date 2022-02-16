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

struct iwl_mvm_vif;
struct iwl_mvm_sta;

// for testing
size_t compose_band_list(const struct iwl_nvm_data* nvm_data,
                         wlan_band_t bands[WLAN_INFO_MAX_BANDS]);
void fill_band_cap_list(const struct iwl_nvm_data* nvm_data, const wlan_band_t* bands,
                        size_t band_cap_count, wlan_softmac_band_capability_t* band_cap_list);

// Phy protocol helpers
zx_status_t phy_get_supported_mac_roles(
    void* ctx,
    wlan_mac_role_t out_supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
    uint8_t* out_supported_mac_roles_count);
zx_status_t phy_create_iface(void* ctx, const wlanphy_impl_create_iface_req_t* req,
                             uint16_t* out_iface_id);
zx_status_t phy_start_iface(void* ctx, zx_device_t* zxdev, uint16_t idx);
zx_status_t phy_destroy_iface(void* ctx, uint16_t id);
zx_status_t phy_set_country(void* ctx, const wlanphy_country_t* country);
zx_status_t phy_get_country(void* ctx, wlanphy_country_t* out_country);

void phy_create_iface_undo(struct iwl_trans* iwl_trans, uint16_t idx);

// Mac protocol helpers
zx_status_t mac_query(void* ctx, wlan_softmac_info_t* info);
void mac_query_discovery_support(discovery_support_t* out_resp);
void mac_query_mac_sublayer_support(mac_sublayer_support_t* out_resp);
void mac_query_security_support(security_support_t* out_resp);
void mac_query_spectrum_management_support(spectrum_management_support_t* out_resp);

zx_status_t mac_start(void* ctx, const wlan_softmac_ifc_protocol_t* ifc,
                      zx_handle_t* out_mlme_channel);
void mac_stop(struct iwl_mvm_vif* mvmvif);
zx_status_t mac_set_channel(struct iwl_mvm_vif* mvmvif, const wlan_channel_t* channel);
zx_status_t mac_configure_bss(struct iwl_mvm_vif* mvmvif, const bss_config_t* config);
zx_status_t mac_unconfigure_bss(struct iwl_mvm_vif* mvmvif);
zx_status_t mac_enable_beaconing(void* ctx, const wlan_bcn_config_t* bcn_cfg);
zx_status_t mac_configure_beacon(void* ctx, const wlan_tx_packet_t* packet_template);
zx_status_t mac_configure_assoc(struct iwl_mvm_vif* mvmvif, const wlan_assoc_ctx_t* assoc_ctx);
zx_status_t mac_clear_assoc(struct iwl_mvm_vif* mvmvif,
                            const uint8_t peer_addr[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]);
zx_status_t mac_start_passive_scan(void* ctx,
                                   const wlan_softmac_passive_scan_args_t* passive_scan_args,
                                   uint64_t* out_scan_id);
zx_status_t mac_start_active_scan(void* ctx,
                                  const wlan_softmac_active_scan_args_t* active_scan_args,
                                  uint64_t* out_scan_id);
zx_status_t mac_init(void* ctx, struct iwl_trans* drvdata, zx_device_t* zxdev, uint16_t idx);
void mac_unbind(void* ctx);
void mac_release(void* ctx);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_MLME_H_
