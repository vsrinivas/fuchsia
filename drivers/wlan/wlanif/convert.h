// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/protocol/if-impl.h>

namespace wlanif {

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;

uint8_t ConvertBSSType(wlan_mlme::BSSTypes bss_type);
uint8_t ConvertScanType(wlan_mlme::ScanTypes scan_type);
uint8_t ConvertCBW(wlan_mlme::CBW cbw);
void ConvertWlanChan(wlan_channel_t* wlanif_chan, const wlan_mlme::WlanChan& fidl_chan);
void ConvertWlanChan(wlan_mlme::WlanChan* fidl_chan, const wlan_channel_t& wlanif_chan);
void CopySSID(const ::fidl::VectorPtr<uint8_t>& in_ssid, wlanif_ssid_t* out_ssid);
void CopyRSNE(const ::fidl::VectorPtr<uint8_t>& in_rsne, uint8_t* out_rsne, size_t* out_rsne_len);
void ConvertBSSDescription(wlanif_bss_description_t* wlanif_bss_desc,
                           const wlan_mlme::BSSDescription& fidl_bss_desc);
void ConvertBSSDescription(wlan_mlme::BSSDescription* fidl_bss_desc,
                           const wlanif_bss_description_t& wlanif_bss_desc);
uint8_t ConvertAuthType(wlan_mlme::AuthenticationTypes auth_type);
uint16_t ConvertDeauthReasonCode(wlan_mlme::ReasonCode reason);
uint8_t ConvertKeyType(wlan_mlme::KeyType key_type);
void ConvertSetKeyDescriptor(set_key_descriptor_t* key_desc,
                             const wlan_mlme::SetKeyDescriptor& fidl_key_desc);
void ConvertDeleteKeyDescriptor(delete_key_descriptor_t* key_desc,
                                const wlan_mlme::DeleteKeyDescriptor& fidl_key_desc);
wlan_mlme::BSSTypes ConvertBSSType(uint8_t bss_type);
wlan_mlme::CBW ConvertCBW(uint8_t cbw);
wlan_mlme::AuthenticationTypes ConvertAuthType(uint8_t auth_type);
wlan_mlme::ReasonCode ConvertDeauthReasonCode(uint16_t reason);
wlan_mlme::ScanResultCodes ConvertScanResultCode(uint8_t code);
wlan_mlme::JoinResultCodes ConvertJoinResultCode(uint8_t code);
wlan_mlme::AuthenticateResultCodes ConvertAuthResultCode(uint8_t code);
uint8_t ConvertAuthResultCode(wlan_mlme::AuthenticateResultCodes result_code);
wlan_mlme::AssociateResultCodes ConvertAssocResultCode(uint8_t code);
uint8_t ConvertAssocResultCode(wlan_mlme::AssociateResultCodes code);
wlan_mlme::StartResultCodes ConvertStartResultCode(uint8_t code);
wlan_mlme::EapolResultCodes ConvertEapolResultCode(uint8_t code);
wlan_mlme::MacRole ConvertMacRole(uint8_t role);
void ConvertBandCapabilities(wlan_mlme::BandCapabilities* fidl_band,
                             const wlanif_band_capabilities_t& band);
void ConvertIfaceStats(wlan_stats::IfaceStats* fidl_stats, const wlanif_stats_t& stats);

}  // namespace wlanif
