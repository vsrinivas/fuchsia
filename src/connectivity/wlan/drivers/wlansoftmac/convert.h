// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANSOFTMAC_CONVERT_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANSOFTMAC_CONVERT_H_

#include <fidl/fuchsia.wlan.softmac/cpp/driver/wire.h>
#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <net/ethernet.h>

namespace wlan {

#define PRE_ALLOC_RECV_BUFFER_SIZE 2000
static uint8_t* pre_alloc_recv_buffer = static_cast<uint8_t*>(malloc(PRE_ALLOC_RECV_BUFFER_SIZE));

// FIDL to banjo conversions.
zx_status_t ConvertWlanSoftmacInfo(const fuchsia_wlan_softmac::wire::WlanSoftmacInfo& in,
                                   wlan_softmac_info_t* out);
void ConvertDiscoverySuppport(const fuchsia_wlan_common::wire::DiscoverySupport& in,
                              discovery_support_t* out);
zx_status_t ConvertMacSublayerSupport(const fuchsia_wlan_common::wire::MacSublayerSupport& in,
                                      mac_sublayer_support_t* out);
void ConvertSecuritySupport(const fuchsia_wlan_common::wire::SecuritySupport& in,
                            security_support_t* out);
void ConvertSpectrumManagementSupport(
    const fuchsia_wlan_common::wire::SpectrumManagementSupport& in,
    spectrum_management_support_t* out);
zx_status_t ConvertRxPacket(const fuchsia_wlan_softmac::wire::WlanRxPacket& in,
                            wlan_rx_packet_t* out);
zx_status_t ConvertTxStatus(const fuchsia_wlan_common::wire::WlanTxStatus& in,
                            wlan_tx_status_t* out);

// banjo to FIDL conversions.
zx_status_t ConvertMacRole(const wlan_mac_role_t& in, fuchsia_wlan_common::wire::WlanMacRole* out);
zx_status_t ConvertTxPacket(const uint8_t* data_in, const size_t data_len_in,
                            const wlan_tx_info_t& info_in,
                            fuchsia_wlan_softmac::wire::WlanTxPacket* out);
zx_status_t ConvertChannel(const wlan_channel_t& in, fuchsia_wlan_common::wire::WlanChannel* out);
zx_status_t ConvertBssConfig(const bss_config_t& in, fuchsia_wlan_internal::wire::BssConfig* out);
void ConvertBcn(const wlan_bcn_config_t& in, fuchsia_wlan_softmac::wire::WlanBcnConfig* out,
                fidl::AnyArena& arena);
zx_status_t ConvertKeyConfig(const wlan_key_config_t& in,
                             fuchsia_wlan_softmac::wire::WlanKeyConfig* out, fidl::AnyArena& arena);
void ConvertPassiveScanArgs(const wlan_softmac_passive_scan_args_t& in,
                            fuchsia_wlan_softmac::wire::WlanSoftmacPassiveScanArgs* out,
                            fidl::AnyArena& arena);
void ConvertActiveScanArgs(const wlan_softmac_active_scan_args_t& in,
                           fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs* out,
                           fidl::AnyArena& arena);
zx_status_t ConvertAssocCtx(const wlan_assoc_ctx_t& in,
                            fuchsia_hardware_wlan_associnfo::wire::WlanAssocCtx* out);

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANSOFTMAC_CONVERT_H_
