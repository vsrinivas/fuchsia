// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "convert.h"

#include <algorithm>
#include <bitset>
#include <memory>

#include <ddk/hw/wlan/wlaninfo/c/banjo.h>
#include <wlan/common/band.h>
#include <wlan/common/logging.h>

#include "fuchsia/wlan/stats/cpp/fidl.h"

namespace wlanif {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;
namespace wlan_internal = ::fuchsia::wlan::internal;
namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;

namespace {

template <typename T>
constexpr bool IsValidReasonCode(T reason_code) {
  switch (reason_code) {
    case static_cast<T>(wlan_mlme::ReasonCode::UNSPECIFIED_REASON):
    case static_cast<T>(wlan_mlme::ReasonCode::INVALID_AUTHENTICATION):
    case static_cast<T>(wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INACTIVITY):
    case static_cast<T>(wlan_mlme::ReasonCode::NO_MORE_STAS):
    case static_cast<T>(wlan_mlme::ReasonCode::INVALID_CLASS2_FRAME):
    case static_cast<T>(wlan_mlme::ReasonCode::INVALID_CLASS3_FRAME):
    case static_cast<T>(wlan_mlme::ReasonCode::LEAVING_NETWORK_DISASSOC):
    case static_cast<T>(wlan_mlme::ReasonCode::NOT_AUTHENTICATED):
    case static_cast<T>(wlan_mlme::ReasonCode::UNACCEPTABLE_POWER_CA):
    case static_cast<T>(wlan_mlme::ReasonCode::UNACCEPTABLE_SUPPORTED_CHANNELS):
    case static_cast<T>(wlan_mlme::ReasonCode::BSS_TRANSITION_DISASSOC):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INVALID_ELEMENT):
    case static_cast<T>(wlan_mlme::ReasonCode::MIC_FAILURE):
    case static_cast<T>(wlan_mlme::ReasonCode::FOURWAY_HANDSHAKE_TIMEOUT):
    case static_cast<T>(wlan_mlme::ReasonCode::GK_HANDSHAKE_TIMEOUT):
    case static_cast<T>(wlan_mlme::ReasonCode::HANDSHAKE_ELEMENT_MISMATCH):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INVALID_GROUP_CIPHER):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INVALID_PAIRWISE_CIPHER):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INVALID_AKMP):
    case static_cast<T>(wlan_mlme::ReasonCode::UNSUPPORTED_RSNE_VERSION):
    case static_cast<T>(wlan_mlme::ReasonCode::INVALID_RSNE_CAPABILITIES):
    case static_cast<T>(wlan_mlme::ReasonCode::IEEE802_1_X_AUTH_FAILED):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_CIPHER_OUT_OF_POLICY):
    case static_cast<T>(wlan_mlme::ReasonCode::TDLS_PEER_UNREACHABLE):
    case static_cast<T>(wlan_mlme::ReasonCode::TDLS_UNSPECIFIED_REASON):
    case static_cast<T>(wlan_mlme::ReasonCode::SSP_REQUESTED_DISASSOC):
    case static_cast<T>(wlan_mlme::ReasonCode::NO_SSP_ROAMING_AGREEMENT):
    case static_cast<T>(wlan_mlme::ReasonCode::BAD_CIPHER_OR_AKM):
    case static_cast<T>(wlan_mlme::ReasonCode::NOT_AUTHORIZED_THIS_LOCATION):
    case static_cast<T>(wlan_mlme::ReasonCode::SERVICE_CHANGE_PRECLUDES_TS):
    case static_cast<T>(wlan_mlme::ReasonCode::UNSPECIFIED_QOS_REASON):
    case static_cast<T>(wlan_mlme::ReasonCode::NOT_ENOUGH_BANDWIDTH):
    case static_cast<T>(wlan_mlme::ReasonCode::MISSING_ACKS):
    case static_cast<T>(wlan_mlme::ReasonCode::EXCEEDED_TXOP):
    case static_cast<T>(wlan_mlme::ReasonCode::STA_LEAVING):
    case static_cast<T>(wlan_mlme::ReasonCode::END_TS_BA_DLS):
    case static_cast<T>(wlan_mlme::ReasonCode::UNKNOWN_TS_BA):
    case static_cast<T>(wlan_mlme::ReasonCode::TIMEOUT):
    case static_cast<T>(wlan_mlme::ReasonCode::PEERKEY_MISMATCH):
    case static_cast<T>(wlan_mlme::ReasonCode::PEER_INITIATED):
    case static_cast<T>(wlan_mlme::ReasonCode::AP_INITIATED):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INVALID_FT_ACTION_FRAME_COUNT):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INVALID_PMKID):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INVALID_MDE):
    case static_cast<T>(wlan_mlme::ReasonCode::REASON_INVALID_FTE):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_PEERING_CANCELED):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_MAX_PEERS):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_CONFIGURATION_POLICY_VIOLATION):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_CLOSE_RCVD):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_MAX_RETRIES):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_CONFIRM_TIMEOUT):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_INVALID_GTK):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_INCONSISTENT_PARAMETERS):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_INVALID_SECURITY_CAPABILITY):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_PATH_ERROR_NO_PROXY_INFORMATION):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_PATH_ERROR_NO_FORWARDING_INFORMATION):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_PATH_ERROR_DESTINATION_UNREACHABLE):
    case static_cast<T>(wlan_mlme::ReasonCode::MAC_ADDRESS_ALREADY_EXISTS_IN_MBSS):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_CHANNEL_SWITCH_REGULATORY_REQUIREMENTS):
    case static_cast<T>(wlan_mlme::ReasonCode::MESH_CHANNEL_SWITCH_UNSPECIFIED):
      return true;
    default:
      return false;
  }
}

template <typename T>
constexpr bool IsValidStatusCode(T status_code) {
  switch (status_code) {
    case static_cast<T>(wlan_ieee80211::StatusCode::SUCCESS):
    case static_cast<T>(wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED):
    case static_cast<T>(wlan_ieee80211::StatusCode::TDLS_REJECTED_ALTERNATIVE_PROVIDED):
    case static_cast<T>(wlan_ieee80211::StatusCode::TDLS_REJECTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::SECURITY_DISABLED):
    case static_cast<T>(wlan_ieee80211::StatusCode::UNACCEPTABLE_LIFETIME):
    case static_cast<T>(wlan_ieee80211::StatusCode::NOT_IN_SAME_BSS):
    case static_cast<T>(wlan_ieee80211::StatusCode::REFUSED_CAPABILITIES_MISMATCH):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_NO_ASSOCIATION_EXISTS):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_OTHER_REASON):
    case static_cast<T>(wlan_ieee80211::StatusCode::UNSUPPORTED_AUTH_ALGORITHM):
    case static_cast<T>(wlan_ieee80211::StatusCode::TRANSACTION_SEQUENCE_ERROR):
    case static_cast<T>(wlan_ieee80211::StatusCode::CHALLENGE_FAILURE):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_SEQUENCE_TIMEOUT):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_NO_MORE_STAS):
    case static_cast<T>(wlan_ieee80211::StatusCode::REFUSED_BASIC_RATES_MISMATCH):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_NO_SHORT_PREAMBLE_SUPPORT):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_SPECTRUM_MANAGEMENT_REQUIRED):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_BAD_POWER_CAPABILITY):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_BAD_SUPPORTED_CHANNELS):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_NO_SHORT_SLOT_TIME_SUPPORT):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_NO_HT_SUPPORT):
    case static_cast<T>(wlan_ieee80211::StatusCode::R0KH_UNREACHABLE):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_PCO_TIME_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::REFUSED_TEMPORARILY):
    case static_cast<T>(wlan_ieee80211::StatusCode::ROBUST_MANAGEMENT_POLICY_VIOLATION):
    case static_cast<T>(wlan_ieee80211::StatusCode::UNSPECIFIED_QOS_FAILURE):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_INSUFFICIENT_BANDWIDTH):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_POOR_CHANNEL_CONDITIONS):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_QOS_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::REQUEST_DECLINED):
    case static_cast<T>(wlan_ieee80211::StatusCode::INVALID_PARAMETERS):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_WITH_SUGGESTED_CHANGES):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_INVALID_ELEMENT):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_INVALID_GROUP_CIPHER):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_INVALID_PAIRWISE_CIPHER):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_INVALID_AKMP):
    case static_cast<T>(wlan_ieee80211::StatusCode::UNSUPPORTED_RSNE_VERSION):
    case static_cast<T>(wlan_ieee80211::StatusCode::INVALID_RSNE_CAPABILITIES):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_CIPHER_OUT_OF_POLICY):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_FOR_DELAY_PERIOD):
    case static_cast<T>(wlan_ieee80211::StatusCode::DLS_NOT_ALLOWED):
    case static_cast<T>(wlan_ieee80211::StatusCode::NOT_PRESENT):
    case static_cast<T>(wlan_ieee80211::StatusCode::NOT_QOS_STA):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_LISTEN_INTERVAL_TOO_LARGE):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_INVALID_FT_ACTION_FRAME_COUNT):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_INVALID_PMKID):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_INVALID_MDE):
    case static_cast<T>(wlan_ieee80211::StatusCode::STATUS_INVALID_FTE):
    case static_cast<T>(wlan_ieee80211::StatusCode::REQUESTED_TCLAS_NOT_SUPPORTED_BY_AP):
    case static_cast<T>(wlan_ieee80211::StatusCode::INSUFFICIENT_TCLAS_PROCESSING_RESOURCES):
    case static_cast<T>(wlan_ieee80211::StatusCode::TRY_ANOTHER_BSS):
    case static_cast<T>(wlan_ieee80211::StatusCode::GAS_ADVERTISEMENT_PROTOCOL_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::NO_OUTSTANDING_GAS_REQUEST):
    case static_cast<T>(wlan_ieee80211::StatusCode::GAS_RESPONSE_NOT_RECEIVED_FROM_SERVER):
    case static_cast<T>(wlan_ieee80211::StatusCode::GAS_QUERY_TIMEOUT):
    case static_cast<T>(wlan_ieee80211::StatusCode::GAS_QUERY_RESPONSE_TOO_LARGE):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_HOME_WITH_SUGGESTED_CHANGES):
    case static_cast<T>(wlan_ieee80211::StatusCode::SERVER_UNREACHABLE):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_FOR_SSP_PERMISSIONS):
    case static_cast<T>(wlan_ieee80211::StatusCode::REFUSED_UNAUTHENTICATED_ACCESS_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::INVALID_RSNE):
    case static_cast<T>(wlan_ieee80211::StatusCode::U_APSD_COEXISTANCE_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::U_APSD_COEX_MODE_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::BAD_INTERVAL_WITH_U_APSD_COEX):
    case static_cast<T>(wlan_ieee80211::StatusCode::ANTI_CLOGGING_TOKEN_REQUIRED):
    case static_cast<T>(wlan_ieee80211::StatusCode::UNSUPPORTED_FINITE_CYCLIC_GROUP):
    case static_cast<T>(wlan_ieee80211::StatusCode::CANNOT_FIND_ALTERNATIVE_TBTT):
    case static_cast<T>(wlan_ieee80211::StatusCode::TRANSMISSION_FAILURE):
    case static_cast<T>(wlan_ieee80211::StatusCode::REQUESTED_TCLAS_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::TCLAS_RESOURCES_EXHAUSTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_WITH_SUGGESTED_BSS_TRANSITION):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECT_WITH_SCHEDULE):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECT_NO_WAKEUP_SPECIFIED):
    case static_cast<T>(wlan_ieee80211::StatusCode::SUCCESS_POWER_SAVE_MODE):
    case static_cast<T>(wlan_ieee80211::StatusCode::PENDING_ADMITTING_FST_SESSION):
    case static_cast<T>(wlan_ieee80211::StatusCode::PERFORMING_FST_NOW):
    case static_cast<T>(wlan_ieee80211::StatusCode::PENDING_GAP_IN_BA_WINDOW):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECT_U_PID_SETTING):
    case static_cast<T>(wlan_ieee80211::StatusCode::REFUSED_EXTERNAL_REASON):
    case static_cast<T>(wlan_ieee80211::StatusCode::REFUSED_AP_OUT_OF_MEMORY):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::QUERY_RESPONSE_OUTSTANDING):
    case static_cast<T>(wlan_ieee80211::StatusCode::REJECT_DSE_BAND):
    case static_cast<T>(wlan_ieee80211::StatusCode::TCLAS_PROCESSING_TERMINATED):
    case static_cast<T>(wlan_ieee80211::StatusCode::TS_SCHEDULE_CONFLICT):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_WITH_SUGGESTED_BAND_AND_CHANNEL):
    case static_cast<T>(wlan_ieee80211::StatusCode::MCCAOP_RESERVATION_CONFLICT):
    case static_cast<T>(wlan_ieee80211::StatusCode::MAF_LIMIT_EXCEEDED):
    case static_cast<T>(wlan_ieee80211::StatusCode::MCCA_TRACK_LIMIT_EXCEEDED):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_DUE_TO_SPECTRUM_MANAGEMENT):
    case static_cast<T>(wlan_ieee80211::StatusCode::DENIED_VHT_NOT_SUPPORTED):
    case static_cast<T>(wlan_ieee80211::StatusCode::ENABLEMENT_DENIED):
    case static_cast<T>(wlan_ieee80211::StatusCode::RESTRICTION_FROM_AUTHORIZED_GDB):
    case static_cast<T>(wlan_ieee80211::StatusCode::AUTHORIZATION_DEENABLED):
      return true;
    default:
      return false;
  }
}

}  // namespace

uint8_t ConvertBssType(wlan_internal::BssTypes bss_type) {
  switch (bss_type) {
    case wlan_internal::BssTypes::INFRASTRUCTURE:
      return WLAN_BSS_TYPE_INFRASTRUCTURE;
    case wlan_internal::BssTypes::PERSONAL:
      return WLAN_BSS_TYPE_PERSONAL;
    case wlan_internal::BssTypes::INDEPENDENT:
      return WLAN_BSS_TYPE_IBSS;
    case wlan_internal::BssTypes::MESH:
      return WLAN_BSS_TYPE_MESH;
    case wlan_internal::BssTypes::ANY_BSS:
      return WLAN_BSS_TYPE_ANY_BSS;
    default:
      ZX_ASSERT(0);
  }
}

uint8_t ConvertScanType(wlan_mlme::ScanTypes scan_type) {
  switch (scan_type) {
    case wlan_mlme::ScanTypes::ACTIVE:
      return WLAN_SCAN_TYPE_ACTIVE;
    case wlan_mlme::ScanTypes::PASSIVE:
      return WLAN_SCAN_TYPE_PASSIVE;
    default:
      ZX_ASSERT(0);
  }
}

uint8_t ConvertCBW(wlan_common::CBW cbw) {
  switch (cbw) {
    case wlan_common::CBW::CBW20:
      return WLAN_CHANNEL_BANDWIDTH__20;
    case wlan_common::CBW::CBW40:
      return WLAN_CHANNEL_BANDWIDTH__40;
    case wlan_common::CBW::CBW40BELOW:
      return WLAN_CHANNEL_BANDWIDTH__40BELOW;
    case wlan_common::CBW::CBW80:
      return WLAN_CHANNEL_BANDWIDTH__80;
    case wlan_common::CBW::CBW160:
      return WLAN_CHANNEL_BANDWIDTH__160;
    case wlan_common::CBW::CBW80P80:
      return WLAN_CHANNEL_BANDWIDTH__80P80;
  }
  ZX_ASSERT(0);
}

void ConvertWlanChan(wlan_channel_t* wlanif_chan, const wlan_common::WlanChan& fidl_chan) {
  // primary
  wlanif_chan->primary = fidl_chan.primary;

  // CBW
  wlanif_chan->cbw = ConvertCBW(fidl_chan.cbw);

  // secondary80
  wlanif_chan->secondary80 = fidl_chan.secondary80;
}

void CopySSID(const ::std::vector<uint8_t>& in_ssid, wlanif_ssid_t* out_ssid) {
  size_t ssid_len = in_ssid.size();
  if (ssid_len > WLAN_MAX_SSID_LEN) {
    warnf("wlanif: truncating ssid from %zu to %d\n", ssid_len, WLAN_MAX_SSID_LEN);
    ssid_len = WLAN_MAX_SSID_LEN;
  }
  std::memcpy(out_ssid->data, in_ssid.data(), ssid_len);
  out_ssid->len = ssid_len;
}

void CopyCountry(const ::std::vector<uint8_t>& in_country, uint8_t* out_country,
                 size_t* out_country_len) {
  if (in_country.size() > WLAN_IE_BODY_MAX_LEN) {
    warnf("wlanif: Country length truncated from %lu to %d\n", in_country.size(),
          WLAN_IE_BODY_MAX_LEN);
    *out_country_len = WLAN_IE_BODY_MAX_LEN;
  } else {
    *out_country_len = in_country.size();
  }
  std::memcpy(out_country, in_country.data(), *out_country_len);
}

void CopyRSNE(const ::std::vector<uint8_t>& in_rsne, uint8_t* out_rsne, size_t* out_rsne_len) {
  if (in_rsne.size() > WLAN_IE_BODY_MAX_LEN) {
    warnf("wlanif: RSNE length truncated from %lu to %d\n", in_rsne.size(), WLAN_IE_BODY_MAX_LEN);
    *out_rsne_len = WLAN_IE_BODY_MAX_LEN;
  } else {
    *out_rsne_len = in_rsne.size();
  }
  std::memcpy(out_rsne, in_rsne.data(), *out_rsne_len);
}

void CopyVendorSpecificIE(const ::std::vector<uint8_t>& in_vendor_specific,
                          uint8_t* out_vendor_specific, size_t* vendor_specific_len) {
  if (in_vendor_specific.size() > WLAN_VIE_MAX_LEN) {
    warnf("wlanif: Vendor Specific length truncated from %lu to %d\n", in_vendor_specific.size(),
          WLAN_VIE_MAX_LEN);
    *vendor_specific_len = WLAN_VIE_MAX_LEN;
  } else {
    *vendor_specific_len = in_vendor_specific.size();
  }
  std::memcpy(out_vendor_specific, in_vendor_specific.data(), *vendor_specific_len);
}

void ConvertBssDescription(wlanif_bss_description_t* wlanif_desc,
                           const wlan_internal::BssDescription& fidl_desc) {
  // bssid
  std::memcpy(wlanif_desc->bssid, fidl_desc.bssid.data(), ETH_ALEN);

  // bss_type
  wlanif_desc->bss_type = ConvertBssType(fidl_desc.bss_type);

  // beacon_period
  wlanif_desc->beacon_period = fidl_desc.beacon_period;

  // timestamp
  wlanif_desc->timestamp = fidl_desc.timestamp;

  // local_time
  wlanif_desc->local_time = fidl_desc.local_time;

  // capability
  wlanif_desc->cap = fidl_desc.cap;

  // ies
  wlanif_desc->ies_bytes_list = fidl_desc.ies.data();
  wlanif_desc->ies_bytes_count = fidl_desc.ies.size();

  // chan
  ConvertWlanChan(&wlanif_desc->chan, fidl_desc.chan);

  // rssi_dbm
  wlanif_desc->rssi_dbm = fidl_desc.rssi_dbm;

  // snr_db
  wlanif_desc->snr_db = fidl_desc.snr_db;
}

wlan_internal::BssTypes ConvertBssType(uint8_t bss_type) {
  switch (bss_type) {
    case WLAN_BSS_TYPE_INFRASTRUCTURE:
      return wlan_internal::BssTypes::INFRASTRUCTURE;
    case WLAN_BSS_TYPE_PERSONAL:
      return wlan_internal::BssTypes::PERSONAL;
    case WLAN_BSS_TYPE_IBSS:
      return wlan_internal::BssTypes::INDEPENDENT;
    case WLAN_BSS_TYPE_MESH:
      return wlan_internal::BssTypes::MESH;
    case WLAN_BSS_TYPE_ANY_BSS:
      return wlan_internal::BssTypes::ANY_BSS;
    default:
      ZX_ASSERT(0);
  }
}

wlan_common::CBW ConvertCBW(wlan_channel_bandwidth_t cbw) {
  switch (cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      return wlan_common::CBW::CBW20;
    case WLAN_CHANNEL_BANDWIDTH__40:
      return wlan_common::CBW::CBW40;
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      return wlan_common::CBW::CBW40BELOW;
    case WLAN_CHANNEL_BANDWIDTH__80:
      return wlan_common::CBW::CBW80;
    case WLAN_CHANNEL_BANDWIDTH__160:
      return wlan_common::CBW::CBW160;
    case WLAN_CHANNEL_BANDWIDTH__80P80:
      return wlan_common::CBW::CBW80P80;
    default:
      ZX_ASSERT(0);
  }
}

void ConvertWlanChan(wlan_common::WlanChan* fidl_chan, const wlan_channel_t& wlanif_chan) {
  // primary
  fidl_chan->primary = wlanif_chan.primary;

  // CBW
  fidl_chan->cbw = ConvertCBW(wlanif_chan.cbw);

  // secondary80
  fidl_chan->secondary80 = wlanif_chan.secondary80;
}

void ConvertBssDescription(wlan_internal::BssDescription* fidl_desc,
                           const wlanif_bss_description_t& wlanif_desc) {
  // bssid
  std::memcpy(fidl_desc->bssid.data(), wlanif_desc.bssid, ETH_ALEN);

  // bss_type
  fidl_desc->bss_type = ConvertBssType(wlanif_desc.bss_type);

  // beacon_period
  fidl_desc->beacon_period = wlanif_desc.beacon_period;

  // timestamp
  fidl_desc->timestamp = wlanif_desc.timestamp;

  // local_time
  fidl_desc->local_time = wlanif_desc.local_time;

  // capability
  fidl_desc->cap = wlanif_desc.cap;

  // ies
  if (wlanif_desc.ies_bytes_count > 0) {
    fidl_desc->ies = std::vector<uint8_t>(wlanif_desc.ies_bytes_list,
                                          wlanif_desc.ies_bytes_list + wlanif_desc.ies_bytes_count);
  }

  // chan
  ConvertWlanChan(&fidl_desc->chan, wlanif_desc.chan);

  // rssi_dbm
  fidl_desc->rssi_dbm = wlanif_desc.rssi_dbm;

  // snr_db
  fidl_desc->snr_db = wlanif_desc.snr_db;
}

void ConvertAssocInd(wlan_mlme::AssociateIndication* fidl_ind,
                     const wlanif_assoc_ind_t& assoc_ind) {
  *fidl_ind = {};
  // peer_sta_address
  std::memcpy(fidl_ind->peer_sta_address.data(), assoc_ind.peer_sta_address, ETH_ALEN);

  // listen_interval
  fidl_ind->listen_interval = assoc_ind.listen_interval;

  // ssid
  if (assoc_ind.ssid.len) {
    fidl_ind->ssid = {
        std::vector<uint8_t>(assoc_ind.ssid.data, assoc_ind.ssid.data + assoc_ind.ssid.len)};
  }
  // rsne
  bool is_protected = assoc_ind.rsne_len != 0;
  if (is_protected) {
    fidl_ind->rsne = {std::vector<uint8_t>(assoc_ind.rsne, assoc_ind.rsne + assoc_ind.rsne_len)};
  }
}

uint8_t ConvertAuthType(wlan_mlme::AuthenticationTypes auth_type) {
  switch (auth_type) {
    case wlan_mlme::AuthenticationTypes::OPEN_SYSTEM:
      return WLAN_AUTH_TYPE_OPEN_SYSTEM;
    case wlan_mlme::AuthenticationTypes::SHARED_KEY:
      return WLAN_AUTH_TYPE_SHARED_KEY;
    case wlan_mlme::AuthenticationTypes::FAST_BSS_TRANSITION:
      return WLAN_AUTH_TYPE_FAST_BSS_TRANSITION;
    case wlan_mlme::AuthenticationTypes::SAE:
      return WLAN_AUTH_TYPE_SAE;
    default:
      ZX_ASSERT(0);
  }
}

uint16_t ConvertReasonCode(wlan_mlme::ReasonCode reason) {
  ZX_ASSERT(IsValidReasonCode(reason));
  return static_cast<uint16_t>(reason);
}

uint16_t ConvertStatusCode(wlan_ieee80211::StatusCode status) {
  ZX_ASSERT(IsValidStatusCode(status));
  return static_cast<uint16_t>(status);
}

uint8_t ConvertKeyType(wlan_mlme::KeyType key_type) {
  switch (key_type) {
    case wlan_mlme::KeyType::GROUP:
      return WLAN_KEY_TYPE_GROUP;
    case wlan_mlme::KeyType::PAIRWISE:
      return WLAN_KEY_TYPE_PAIRWISE;
    case wlan_mlme::KeyType::PEER_KEY:
      return WLAN_KEY_TYPE_PEER;
    case wlan_mlme::KeyType::IGTK:
      return WLAN_KEY_TYPE_IGTK;
    default:
      ZX_ASSERT(0);
  }
}

void ConvertSetKeyDescriptor(set_key_descriptor_t* key_desc,
                             const wlan_mlme::SetKeyDescriptor& fidl_key_desc) {
  // key
  key_desc->key_list = const_cast<uint8_t*>(fidl_key_desc.key.data());

  // length
  key_desc->key_count = fidl_key_desc.key.size();

  // key_id
  key_desc->key_id = fidl_key_desc.key_id;

  // key_type
  key_desc->key_type = ConvertKeyType(fidl_key_desc.key_type);

  // address
  std::memcpy(key_desc->address, fidl_key_desc.address.data(), ETH_ALEN);

  // rsc
  key_desc->rsc = fidl_key_desc.rsc;

  // cipher_suite_oui
  std::memcpy(key_desc->cipher_suite_oui, fidl_key_desc.cipher_suite_oui.data(),
              sizeof(key_desc->cipher_suite_oui));

  // cipher_suite_type
  key_desc->cipher_suite_type = fidl_key_desc.cipher_suite_type;
}

void ConvertDeleteKeyDescriptor(delete_key_descriptor_t* key_desc,
                                const wlan_mlme::DeleteKeyDescriptor& fidl_key_desc) {
  // key_id
  key_desc->key_id = fidl_key_desc.key_id;

  // key_type
  key_desc->key_type = ConvertKeyType(fidl_key_desc.key_type);

  // address
  std::memcpy(key_desc->address, fidl_key_desc.address.data(), ETH_ALEN);
}

wlan_mlme::ScanResultCodes ConvertScanResultCode(uint8_t code) {
  switch (code) {
    case WLAN_SCAN_RESULT_SUCCESS:
      return wlan_mlme::ScanResultCodes::SUCCESS;
    case WLAN_SCAN_RESULT_NOT_SUPPORTED:
      return wlan_mlme::ScanResultCodes::NOT_SUPPORTED;
    case WLAN_SCAN_RESULT_INVALID_ARGS:
      return wlan_mlme::ScanResultCodes::INVALID_ARGS;
    case WLAN_SCAN_RESULT_INTERNAL_ERROR:
      return wlan_mlme::ScanResultCodes::INTERNAL_ERROR;
    case WLAN_SCAN_RESULT_SHOULD_WAIT:
      return wlan_mlme::ScanResultCodes::SHOULD_WAIT;
    default:
      ZX_ASSERT(0);
  }
}

wlan_mlme::AuthenticationTypes ConvertAuthType(uint8_t auth_type) {
  switch (auth_type) {
    case WLAN_AUTH_TYPE_OPEN_SYSTEM:
      return wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;
    case WLAN_AUTH_TYPE_SHARED_KEY:
      return wlan_mlme::AuthenticationTypes::SHARED_KEY;
    case WLAN_AUTH_TYPE_FAST_BSS_TRANSITION:
      return wlan_mlme::AuthenticationTypes::FAST_BSS_TRANSITION;
    case WLAN_AUTH_TYPE_SAE:
      return wlan_mlme::AuthenticationTypes::SAE;
    default:
      ZX_ASSERT(0);
  }
}

wlan_mlme::ReasonCode ConvertReasonCode(uint16_t reason) {
  // Use a default for invalid uint16_t reason codes from external sources.
  if (!IsValidReasonCode(reason)) {
    return wlan_mlme::ReasonCode::UNSPECIFIED_REASON;
  }
  return static_cast<wlan_mlme::ReasonCode>(reason);
}

wlan_ieee80211::StatusCode ConvertStatusCode(uint16_t status) {
  // Use a default for invalid uint16_t status codes from external sources.
  if (!IsValidStatusCode(status)) {
    return wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED;
  }
  return static_cast<wlan_ieee80211::StatusCode>(status);
}

wlan_mlme::JoinResultCodes ConvertJoinResultCode(uint8_t code) {
  switch (code) {
    case WLAN_JOIN_RESULT_SUCCESS:
      return wlan_mlme::JoinResultCodes::SUCCESS;
    case WLAN_JOIN_RESULT_FAILURE_TIMEOUT:
      __FALLTHROUGH;
    case WLAN_JOIN_RESULT_INTERNAL_ERROR:
      return wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT;
    default:
      ZX_ASSERT(0);
  }
}

wlan_mlme::AuthenticateResultCodes ConvertAuthResultCode(uint8_t code) {
  switch (code) {
    case WLAN_AUTH_RESULT_SUCCESS:
      return wlan_mlme::AuthenticateResultCodes::SUCCESS;
    case WLAN_AUTH_RESULT_REFUSED:
      return wlan_mlme::AuthenticateResultCodes::REFUSED;
    case WLAN_AUTH_RESULT_ANTI_CLOGGING_TOKEN_REQUIRED:
      return wlan_mlme::AuthenticateResultCodes::ANTI_CLOGGING_TOKEN_REQUIRED;
    case WLAN_AUTH_RESULT_FINITE_CYCLIC_GROUP_NOT_SUPPORTED:
      return wlan_mlme::AuthenticateResultCodes::FINITE_CYCLIC_GROUP_NOT_SUPPORTED;
    case WLAN_AUTH_RESULT_REJECTED:
      return wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED;
    case WLAN_AUTH_RESULT_FAILURE_TIMEOUT:
      return wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT;
    default:
      ZX_ASSERT(0);
  }
}

uint8_t ConvertAuthResultCode(wlan_mlme::AuthenticateResultCodes code) {
  switch (code) {
    case wlan_mlme::AuthenticateResultCodes::SUCCESS:
      return WLAN_AUTH_RESULT_SUCCESS;
    case wlan_mlme::AuthenticateResultCodes::REFUSED:
      return WLAN_AUTH_RESULT_REFUSED;
    case wlan_mlme::AuthenticateResultCodes::ANTI_CLOGGING_TOKEN_REQUIRED:
      return WLAN_AUTH_RESULT_ANTI_CLOGGING_TOKEN_REQUIRED;
    case wlan_mlme::AuthenticateResultCodes::FINITE_CYCLIC_GROUP_NOT_SUPPORTED:
      return WLAN_AUTH_RESULT_FINITE_CYCLIC_GROUP_NOT_SUPPORTED;
    case wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED:
      return WLAN_AUTH_RESULT_REJECTED;
    case wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT:
      return WLAN_AUTH_RESULT_FAILURE_TIMEOUT;
    default:
      ZX_ASSERT(0);
  }
}

wlan_mlme::AssociateResultCodes ConvertAssocResultCode(uint8_t code) {
  switch (code) {
    case WLAN_ASSOC_RESULT_SUCCESS:
      return wlan_mlme::AssociateResultCodes::SUCCESS;
    case WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED:
      return wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED;
    case WLAN_ASSOC_RESULT_REFUSED_NOT_AUTHENTICATED:
      return wlan_mlme::AssociateResultCodes::REFUSED_NOT_AUTHENTICATED;
    case WLAN_ASSOC_RESULT_REFUSED_CAPABILITIES_MISMATCH:
      return wlan_mlme::AssociateResultCodes::REFUSED_CAPABILITIES_MISMATCH;
    case WLAN_ASSOC_RESULT_REFUSED_EXTERNAL_REASON:
      return wlan_mlme::AssociateResultCodes::REFUSED_EXTERNAL_REASON;
    case WLAN_ASSOC_RESULT_REFUSED_AP_OUT_OF_MEMORY:
      return wlan_mlme::AssociateResultCodes::REFUSED_AP_OUT_OF_MEMORY;
    case WLAN_ASSOC_RESULT_REFUSED_BASIC_RATES_MISMATCH:
      return wlan_mlme::AssociateResultCodes::REFUSED_BASIC_RATES_MISMATCH;
    case WLAN_ASSOC_RESULT_REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED:
      return wlan_mlme::AssociateResultCodes::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED;
    case WLAN_ASSOC_RESULT_REFUSED_TEMPORARILY:
      return wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY;
    default:
      ZX_ASSERT(0);
  }
}

uint8_t ConvertAssocResultCode(wlan_mlme::AssociateResultCodes code) {
  switch (code) {
    case wlan_mlme::AssociateResultCodes::SUCCESS:
      return WLAN_ASSOC_RESULT_SUCCESS;
    case wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED:
      return WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED;
    case wlan_mlme::AssociateResultCodes::REFUSED_NOT_AUTHENTICATED:
      return WLAN_ASSOC_RESULT_REFUSED_NOT_AUTHENTICATED;
    case wlan_mlme::AssociateResultCodes::REFUSED_CAPABILITIES_MISMATCH:
      return WLAN_ASSOC_RESULT_REFUSED_CAPABILITIES_MISMATCH;
    case wlan_mlme::AssociateResultCodes::REFUSED_EXTERNAL_REASON:
      return WLAN_ASSOC_RESULT_REFUSED_EXTERNAL_REASON;
    case wlan_mlme::AssociateResultCodes::REFUSED_AP_OUT_OF_MEMORY:
      return WLAN_ASSOC_RESULT_REFUSED_AP_OUT_OF_MEMORY;
    case wlan_mlme::AssociateResultCodes::REFUSED_BASIC_RATES_MISMATCH:
      return WLAN_ASSOC_RESULT_REFUSED_BASIC_RATES_MISMATCH;
    case wlan_mlme::AssociateResultCodes::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED:
      return WLAN_ASSOC_RESULT_REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED;
    case wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY:
      return WLAN_ASSOC_RESULT_REFUSED_TEMPORARILY;
    default:
      ZX_ASSERT(0);
  }
}

wlan_mlme::StartResultCodes ConvertStartResultCode(uint8_t code) {
  switch (code) {
    case WLAN_START_RESULT_SUCCESS:
      return wlan_mlme::StartResultCodes::SUCCESS;
    case WLAN_START_RESULT_BSS_ALREADY_STARTED_OR_JOINED:
      return wlan_mlme::StartResultCodes::BSS_ALREADY_STARTED_OR_JOINED;
    case WLAN_START_RESULT_RESET_REQUIRED_BEFORE_START:
      return wlan_mlme::StartResultCodes::RESET_REQUIRED_BEFORE_START;
    case WLAN_START_RESULT_NOT_SUPPORTED:
      return wlan_mlme::StartResultCodes::NOT_SUPPORTED;
    default:
      ZX_ASSERT(0);
  }
}

wlan_mlme::StopResultCodes ConvertStopResultCode(uint8_t code) {
  switch (code) {
    case WLAN_STOP_RESULT_SUCCESS:
      return wlan_mlme::StopResultCodes::SUCCESS;
    case WLAN_STOP_RESULT_BSS_ALREADY_STOPPED:
      return wlan_mlme::StopResultCodes::BSS_ALREADY_STOPPED;
    case WLAN_STOP_RESULT_INTERNAL_ERROR:
      return wlan_mlme::StopResultCodes::INTERNAL_ERROR;
    default:
      ZX_ASSERT(0);
  }
}

wlan_mlme::EapolResultCodes ConvertEapolResultCode(uint8_t code) {
  switch (code) {
    case WLAN_EAPOL_RESULT_SUCCESS:
      return wlan_mlme::EapolResultCodes::SUCCESS;
    case WLAN_EAPOL_RESULT_TRANSMISSION_FAILURE:
      return wlan_mlme::EapolResultCodes::TRANSMISSION_FAILURE;
    default:
      ZX_ASSERT(0);
  }
}

wlan_mlme::MacRole ConvertMacRole(wlan_info_mac_role_t role) {
  switch (role) {
    case WLAN_INFO_MAC_ROLE_CLIENT:
      return wlan_mlme::MacRole::CLIENT;
    case WLAN_INFO_MAC_ROLE_AP:
      return wlan_mlme::MacRole::AP;
    case WLAN_INFO_MAC_ROLE_MESH:
      return wlan_mlme::MacRole::MESH;
    default:
      ZX_ASSERT(0);
  }
}

void ConvertBandCapabilities(wlan_mlme::BandCapabilities* fidl_band,
                             const wlanif_band_capabilities_t& band) {
  fidl_band->band_id = ::wlan::common::BandToFidl(band.band_id);

  // basic_rates
  fidl_band->rates.assign(band.rates, band.rates + band.num_rates);

  // base_frequency
  fidl_band->base_frequency = band.base_frequency;

  // channels
  fidl_band->channels.assign(band.channels, band.channels + band.num_channels);

  if (band.ht_supported) {
    fidl_band->ht_cap = wlan_internal::HtCapabilities::New();
    static_assert(sizeof(fidl_band->ht_cap->bytes) == sizeof(band.ht_caps));
    memcpy(fidl_band->ht_cap->bytes.data(), &band.ht_caps, sizeof(band.ht_caps));
  }

  if (band.vht_supported) {
    fidl_band->vht_cap = wlan_internal::VhtCapabilities::New();
    static_assert(sizeof(fidl_band->vht_cap->bytes) == sizeof(band.vht_caps));
    memcpy(fidl_band->vht_cap->bytes.data(), &band.vht_caps, sizeof(band.vht_caps));
  }
}

void ConvertCounter(wlan_stats::Counter* fidl_counter, const wlanif_counter_t& counter) {
  fidl_counter->count = counter.count;
  if (counter.name != nullptr) {
    fidl_counter->name = counter.name;
  } else {
    fidl_counter->name = "";
  }
}

void ConvertPacketCounter(wlan_stats::PacketCounter* fidl_counter,
                          const wlanif_packet_counter_t& counter) {
  ConvertCounter(&fidl_counter->in, counter.in);
  ConvertCounter(&fidl_counter->out, counter.out);
  ConvertCounter(&fidl_counter->drop, counter.drop);
  ConvertCounter(&fidl_counter->in_bytes, counter.in_bytes);
  ConvertCounter(&fidl_counter->out_bytes, counter.out_bytes);
  ConvertCounter(&fidl_counter->drop_bytes, counter.drop_bytes);
}

void ConvertDispatcherStats(wlan_stats::DispatcherStats* fidl_stats,
                            const wlanif_dispatcher_stats_t& stats) {
  ConvertPacketCounter(&fidl_stats->any_packet, stats.any_packet);
  ConvertPacketCounter(&fidl_stats->mgmt_frame, stats.mgmt_frame);
  ConvertPacketCounter(&fidl_stats->ctrl_frame, stats.ctrl_frame);
  ConvertPacketCounter(&fidl_stats->data_frame, stats.data_frame);
}

void ConvertRssiStats(wlan_stats::RssiStats* fidl_stats, const wlanif_rssi_stats& stats) {
  fidl_stats->hist.assign(stats.hist_list, stats.hist_list + stats.hist_count);
}

namespace {

// Convert a Banjo antenna ID to a FIDL antenna ID (in a unique_ptr).
std::unique_ptr<wlan_stats::AntennaId> ConvertAntennaId(
    const wlanif_antenna_id_t& impl_antenna_id) {
  auto fidl_antenna_id = std::make_unique<wlan_stats::AntennaId>();
  if (impl_antenna_id.freq == WLANIF_ANTENNA_FREQ_ANTENNA_5_G) {
    fidl_antenna_id->freq = wlan_stats::AntennaFreq::ANTENNA_5_G;
  } else {
    fidl_antenna_id->freq = wlan_stats::AntennaFreq::ANTENNA_2_G;
  }
  fidl_antenna_id->index = impl_antenna_id.index;
  return fidl_antenna_id;
}

}  // namespace

void ConvertNoiseFloorHistogram(wlan_stats::NoiseFloorHistogram* fidl_stats,
                                const wlanif_noise_floor_histogram_t& stats) {
  if (stats.hist_scope == WLANIF_HIST_SCOPE_PER_ANTENNA) {
    fidl_stats->hist_scope = wlan_stats::HistScope::PER_ANTENNA;
    fidl_stats->antenna_id = ConvertAntennaId(stats.antenna_id);
  } else {
    fidl_stats->hist_scope = wlan_stats::HistScope::STATION;
  }
  for (size_t i = 0; i < stats.noise_floor_samples_count; ++i) {
    const auto& bucket = stats.noise_floor_samples_list[i];
    if (bucket.num_samples == 0) {
      // To keep the FIDL histogram compact, we don't add empty buckets.
      continue;
    }
    wlan_stats::HistBucket fidl_bucket;
    fidl_bucket.bucket_index = bucket.bucket_index;
    fidl_bucket.num_samples = bucket.num_samples;
    fidl_stats->noise_floor_samples.push_back(fidl_bucket);
  }
  fidl_stats->invalid_samples = stats.invalid_samples;
}

void ConvertRxRateIndexHistogram(wlan_stats::RxRateIndexHistogram* fidl_stats,
                                 const wlanif_rx_rate_index_histogram_t& stats) {
  if (stats.hist_scope == WLANIF_HIST_SCOPE_PER_ANTENNA) {
    fidl_stats->hist_scope = wlan_stats::HistScope::PER_ANTENNA;
    fidl_stats->antenna_id = ConvertAntennaId(stats.antenna_id);
  } else {
    fidl_stats->hist_scope = wlan_stats::HistScope::STATION;
  }
  for (size_t i = 0; i < stats.rx_rate_index_samples_count; ++i) {
    const auto& bucket = stats.rx_rate_index_samples_list[i];
    if (bucket.num_samples == 0) {
      continue;
    }
    wlan_stats::HistBucket fidl_bucket;
    fidl_bucket.bucket_index = bucket.bucket_index;
    fidl_bucket.num_samples = bucket.num_samples;
    fidl_stats->rx_rate_index_samples.push_back(fidl_bucket);
  }
  fidl_stats->invalid_samples = stats.invalid_samples;
}

void ConvertRssiHistogram(wlan_stats::RssiHistogram* fidl_stats,
                          const wlanif_rssi_histogram_t& stats) {
  if (stats.hist_scope == WLANIF_HIST_SCOPE_PER_ANTENNA) {
    fidl_stats->hist_scope = wlan_stats::HistScope::PER_ANTENNA;
    fidl_stats->antenna_id = ConvertAntennaId(stats.antenna_id);
  } else {
    fidl_stats->hist_scope = wlan_stats::HistScope::STATION;
  }
  for (size_t i = 0; i < stats.rssi_samples_count; ++i) {
    const auto& bucket = stats.rssi_samples_list[i];
    if (bucket.num_samples == 0) {
      continue;
    }
    wlan_stats::HistBucket fidl_bucket;
    fidl_bucket.bucket_index = bucket.bucket_index;
    fidl_bucket.num_samples = bucket.num_samples;
    fidl_stats->rssi_samples.push_back(fidl_bucket);
  }
  fidl_stats->invalid_samples = stats.invalid_samples;
}

void ConvertSnrHistogram(wlan_stats::SnrHistogram* fidl_stats,
                         const wlanif_snr_histogram_t& stats) {
  if (stats.hist_scope == WLANIF_HIST_SCOPE_PER_ANTENNA) {
    fidl_stats->hist_scope = wlan_stats::HistScope::PER_ANTENNA;
    fidl_stats->antenna_id = ConvertAntennaId(stats.antenna_id);
  } else {
    fidl_stats->hist_scope = wlan_stats::HistScope::STATION;
  }
  for (size_t i = 0; i < stats.snr_samples_count; ++i) {
    const auto& bucket = stats.snr_samples_list[i];
    if (bucket.num_samples == 0) {
      continue;
    }
    wlan_stats::HistBucket fidl_bucket;
    fidl_bucket.bucket_index = bucket.bucket_index;
    fidl_bucket.num_samples = bucket.num_samples;
    fidl_stats->snr_samples.push_back(fidl_bucket);
  }
  fidl_stats->invalid_samples = stats.invalid_samples;
}

void ConvertPmkInfo(wlan_mlme::PmkInfo* fidl_info, const wlanif_pmk_info_t& info) {
  fidl_info->pmk.resize(info.pmk_count);
  fidl_info->pmk.assign(info.pmk_list, info.pmk_list + info.pmk_count);
  fidl_info->pmkid.resize(info.pmkid_count);
  fidl_info->pmkid.assign(info.pmkid_list, info.pmkid_list + info.pmkid_count);
}

wlan_stats::ClientMlmeStats BuildClientMlmeStats(const wlanif_client_mlme_stats_t& client_stats) {
  wlan_stats::ClientMlmeStats fidl_client_stats;

  ConvertPacketCounter(&fidl_client_stats.svc_msg, client_stats.svc_msg);
  ConvertPacketCounter(&fidl_client_stats.data_frame, client_stats.data_frame);
  ConvertPacketCounter(&fidl_client_stats.mgmt_frame, client_stats.mgmt_frame);
  ConvertPacketCounter(&fidl_client_stats.tx_frame, client_stats.tx_frame);
  ConvertPacketCounter(&fidl_client_stats.rx_frame, client_stats.rx_frame);

  ConvertRssiStats(&fidl_client_stats.assoc_data_rssi, client_stats.assoc_data_rssi);
  ConvertRssiStats(&fidl_client_stats.beacon_rssi, client_stats.beacon_rssi);

  fidl_client_stats.noise_floor_histograms.resize(client_stats.noise_floor_histograms_count);
  for (size_t i = 0; i < client_stats.noise_floor_histograms_count; ++i) {
    ConvertNoiseFloorHistogram(&fidl_client_stats.noise_floor_histograms[i],
                               client_stats.noise_floor_histograms_list[i]);
  }
  fidl_client_stats.rssi_histograms.resize(client_stats.rssi_histograms_count);
  for (size_t i = 0; i < client_stats.rssi_histograms_count; ++i) {
    ConvertRssiHistogram(&fidl_client_stats.rssi_histograms[i],
                         client_stats.rssi_histograms_list[i]);
  }
  fidl_client_stats.rx_rate_index_histograms.resize(client_stats.rx_rate_index_histograms_count);
  for (size_t i = 0; i < client_stats.rx_rate_index_histograms_count; ++i) {
    ConvertRxRateIndexHistogram(&fidl_client_stats.rx_rate_index_histograms[i],
                                client_stats.rx_rate_index_histograms_list[i]);
  }
  fidl_client_stats.snr_histograms.resize(client_stats.snr_histograms_count);
  for (size_t i = 0; i < client_stats.snr_histograms_count; ++i) {
    ConvertSnrHistogram(&fidl_client_stats.snr_histograms[i], client_stats.snr_histograms_list[i]);
  }

  return fidl_client_stats;
}

wlan_stats::ApMlmeStats BuildApMlmeStats(const wlanif_ap_mlme_stats_t& ap_stats) {
  wlan_stats::ApMlmeStats fidl_ap_stats;

  ConvertPacketCounter(&fidl_ap_stats.not_used, ap_stats.not_used);

  return fidl_ap_stats;
}

void ConvertMlmeStats(wlan_stats::MlmeStats* fidl_stats, const wlanif_mlme_stats_t& stats) {
  switch (stats.tag) {
    case WLANIF_MLME_STATS_TYPE_CLIENT:
      fidl_stats->set_client_mlme_stats(BuildClientMlmeStats(stats.stats.client_mlme_stats));
      break;
    case WLANIF_MLME_STATS_TYPE_AP:
      fidl_stats->set_ap_mlme_stats(BuildApMlmeStats(stats.stats.ap_mlme_stats));
      break;
    default:
      ZX_ASSERT(0);
  }
}

void ConvertIfaceStats(wlan_stats::IfaceStats* fidl_stats, const wlanif_stats_t& stats) {
  ConvertDispatcherStats(&fidl_stats->dispatcher_stats, stats.dispatcher_stats);
  if (stats.mlme_stats_list != nullptr) {
    fidl_stats->mlme_stats = ::std::make_unique<wlan_stats::MlmeStats>();
    ConvertMlmeStats(fidl_stats->mlme_stats.get(), *stats.mlme_stats_list);
  }
}

uint32_t ConvertMgmtCaptureFlags(wlan_mlme::MgmtFrameCaptureFlags fidl_flags) {
  uint32_t ret_flags = 0;
  uint32_t flags = static_cast<uint32_t>(fidl_flags);
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ASSOC_REQ)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_ASSOC_REQ;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ASSOC_RESP)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_ASSOC_RESP;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::REASSOC_REQ)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_REASSOC_REQ;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::REASSOC_RESP)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_REASSOC_RESP;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::PROBE_REQ)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_PROBE_REQ;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::PROBE_RESP)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_PROBE_RESP;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::TIMING_AD)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_TIMING_AD;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::BEACON)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_BEACON;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ATIM)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_ATIM;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::DISASSOC)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_DISASSOC;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::AUTH)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_AUTH;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::DEAUTH)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_DEAUTH;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ACTION)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_ACTION;
  }
  if ((flags & static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ACTION_NO_ACK)) != 0) {
    ret_flags |= WLAN_MGMT_CAPTURE_FLAG_ACTION_NO_ACK;
  }
  return ret_flags;
}

wlan_mlme::MgmtFrameCaptureFlags ConvertMgmtCaptureFlags(uint32_t ddk_flags) {
  uint32_t ret_flags = 0;
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_ASSOC_REQ) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ASSOC_REQ);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_ASSOC_RESP) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ASSOC_RESP);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_REASSOC_REQ) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::REASSOC_REQ);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_REASSOC_RESP) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::REASSOC_RESP);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_PROBE_REQ) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::PROBE_REQ);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_PROBE_RESP) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::PROBE_RESP);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_TIMING_AD) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::TIMING_AD);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_BEACON) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::BEACON);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_ATIM) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ATIM);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_DISASSOC) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::DISASSOC);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_AUTH) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::AUTH);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_DEAUTH) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::DEAUTH);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_ACTION) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ACTION);
  }
  if ((ddk_flags & WLAN_MGMT_CAPTURE_FLAG_ACTION_NO_ACK) != 0) {
    ret_flags |= static_cast<uint32_t>(wlan_mlme::MgmtFrameCaptureFlags::ACTION_NO_ACK);
  }
  return static_cast<wlan_mlme::MgmtFrameCaptureFlags>(ret_flags);
}

void ConvertSaeAuthFrame(const ::fuchsia::wlan::mlme::SaeFrame& frame_in,
                         wlanif_sae_frame_t* frame_out) {
  memcpy(frame_out->peer_sta_address, frame_in.peer_sta_address.data(), ETH_ALEN);
  frame_out->status_code = ConvertStatusCode(frame_in.status_code);
  frame_out->seq_num = frame_in.seq_num;

  frame_out->sae_fields_count = frame_in.sae_fields.size();
  frame_out->sae_fields_list = frame_in.sae_fields.data();
}

void ConvertSaeAuthFrame(const wlanif_sae_frame_t* frame_in,
                         ::fuchsia::wlan::mlme::SaeFrame& frame_out) {
  memcpy(frame_out.peer_sta_address.data(), frame_in->peer_sta_address, ETH_ALEN);
  frame_out.status_code = ConvertStatusCode(frame_in->status_code);
  frame_out.seq_num = frame_in->seq_num;

  frame_out.sae_fields.resize(frame_in->sae_fields_count);
  frame_out.sae_fields.assign(frame_in->sae_fields_list,
                              frame_in->sae_fields_list + frame_in->sae_fields_count);
}

}  // namespace wlanif
