// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the interface between the iwlwifi MVM opmode and the Fuchsia MLME.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_MLME_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_MLME_H_

#include <fidl/fuchsia.wlan.softmac/cpp/driver/wire.h>
#include <fidl/fuchsia.wlan.wlanphyimpl/cpp/driver/wire.h>
#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/cpp/banjo.h>
#include <fuchsia/wlan/common/cpp/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/banjo.h>
#include <lib/ddk/device.h>

// IEEE Std 802.11-2016, Table 9-19
#define WLAN_MSDU_MAX_LEN 2304UL

namespace wlan_ieee80211_wire = fuchsia_wlan_ieee80211::wire;
namespace wlan_softmac_wire = fuchsia_wlan_softmac::wire;
namespace wlan_common_wire = fuchsia_wlan_common::wire;

struct iwl_mvm_vif;
struct iwl_mvm_sta;

// for testing
size_t compose_band_list(const struct iwl_nvm_data* nvm_data,
                         wlan_common_wire::WlanBand bands[fuchsia_wlan_common_MAX_BANDS]);
void fill_band_cap_list(const struct iwl_nvm_data* nvm_data,
                        const wlan_common_wire::WlanBand* bands, size_t band_cap_count,
                        wlan_softmac_wire::WlanSoftmacBandCapability* band_cap_list);

// Phy protocol helpers
zx_status_t phy_get_supported_mac_roles(
    void* ctx,
    wlan_common_wire::WlanMacRole
        out_supported_mac_roles_list[wlan_common_wire::kMaxSupportedMacRoles],
    uint8_t* out_supported_mac_roles_count);
zx_status_t phy_create_iface(void* ctx, const wlanphy_impl_create_iface_req_t* req,
                             uint16_t* out_iface_id);
zx_status_t phy_start_iface(void* ctx, zx_device_t* zxdev, uint16_t idx);
zx_status_t phy_destroy_iface(void* ctx, uint16_t id);
zx_status_t phy_set_country(void* ctx, const wlanphy_country_t* country);
zx_status_t phy_get_country(void* ctx, wlanphy_country_t* out_country);

void phy_create_iface_undo(struct iwl_trans* iwl_trans, uint16_t idx);

// Mac protocol helpers
zx_status_t mac_query(void* ctx, wlan_softmac_wire::WlanSoftmacInfo* info_out,
                      fidl::AnyArena& arena);
void mac_query_discovery_support(wlan_common_wire::DiscoverySupport* out_resp);
void mac_query_mac_sublayer_support(wlan_common_wire::MacSublayerSupport* out_resp);
void mac_query_security_support(wlan_common_wire::SecuritySupport* out_resp);
void mac_query_spectrum_management_support(wlan_common_wire::SpectrumManagementSupport* out_resp);

zx_status_t mac_start(void* ctx, void* ifc_dev, zx_handle_t* out_mlme_channel);
void mac_stop(struct iwl_mvm_vif* mvmvif);
zx_status_t mac_set_channel(struct iwl_mvm_vif* mvmvif, const wlan_channel_t* channel);
zx_status_t mac_configure_bss(struct iwl_mvm_vif* mvmvif,
                              const fuchsia_wlan_internal::wire::BssConfig* config);
zx_status_t mac_unconfigure_bss(struct iwl_mvm_vif* mvmvif);
zx_status_t mac_enable_beaconing(void* ctx, const wlan_softmac_wire::WlanBcnConfig* bcn_cfg);
zx_status_t mac_configure_beacon(void* ctx, const wlan_softmac_wire::WlanTxPacket* packet_template);
zx_status_t mac_configure_assoc(
    struct iwl_mvm_vif* mvmvif,
    const fuchsia_hardware_wlan_associnfo::wire::WlanAssocCtx* assoc_ctx);
zx_status_t mac_clear_assoc(struct iwl_mvm_vif* mvmvif,
                            const uint8_t peer_addr[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]);
zx_status_t mac_start_passive_scan(
    void* ctx, const wlan_softmac_wire::WlanSoftmacPassiveScanArgs* passive_scan_args,
    uint64_t* out_scan_id);
zx_status_t mac_start_active_scan(
    void* ctx, const wlan_softmac_wire::WlanSoftmacActiveScanArgs* active_scan_args,
    uint64_t* out_scan_id);
zx_status_t mac_init(void* ctx, struct iwl_trans* drvdata, zx_device_t* zxdev, uint16_t idx);

// Mid-layer C functions for WlanSoftmacIfc protocol to enter C++ definitions in iwlwifi/platform/
void mac_ifc_recv(void* ctx, const wlan_rx_packet_t* rx_packet);
void mac_ifc_scan_complete(void* ctx, const zx_status_t status, const uint64_t scan_id);

void mac_unbind(void* ctx);
void mac_release(void* ctx);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_MLME_H_
