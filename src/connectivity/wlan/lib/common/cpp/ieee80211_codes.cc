// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/ieee80211_codes.h"

#include <zircon/assert.h>

namespace wlan {
namespace common {

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

namespace {

template <typename T>
constexpr bool IsValidReasonCode(T reason_code) {
  switch (reason_code) {
    case static_cast<T>(wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON):
    case static_cast<T>(wlan_ieee80211::ReasonCode::INVALID_AUTHENTICATION):
    case static_cast<T>(wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DEAUTH):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INACTIVITY):
    case static_cast<T>(wlan_ieee80211::ReasonCode::NO_MORE_STAS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::INVALID_CLASS2_FRAME):
    case static_cast<T>(wlan_ieee80211::ReasonCode::INVALID_CLASS3_FRAME):
    case static_cast<T>(wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DISASSOC):
    case static_cast<T>(wlan_ieee80211::ReasonCode::NOT_AUTHENTICATED):
    case static_cast<T>(wlan_ieee80211::ReasonCode::UNACCEPTABLE_POWER_CAPABILITY):
    case static_cast<T>(wlan_ieee80211::ReasonCode::UNACCEPTABLE_SUPPORTED_CHANNELS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::BSS_TRANSITION_DISASSOC):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INVALID_ELEMENT):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MIC_FAILURE):
    case static_cast<T>(wlan_ieee80211::ReasonCode::FOURWAY_HANDSHAKE_TIMEOUT):
    case static_cast<T>(wlan_ieee80211::ReasonCode::GK_HANDSHAKE_TIMEOUT):
    case static_cast<T>(wlan_ieee80211::ReasonCode::HANDSHAKE_ELEMENT_MISMATCH):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INVALID_GROUP_CIPHER):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INVALID_PAIRWISE_CIPHER):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INVALID_AKMP):
    case static_cast<T>(wlan_ieee80211::ReasonCode::UNSUPPORTED_RSNE_VERSION):
    case static_cast<T>(wlan_ieee80211::ReasonCode::INVALID_RSNE_CAPABILITIES):
    case static_cast<T>(wlan_ieee80211::ReasonCode::IEEE802_1_X_AUTH_FAILED):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_CIPHER_OUT_OF_POLICY):
    case static_cast<T>(wlan_ieee80211::ReasonCode::TDLS_PEER_UNREACHABLE):
    case static_cast<T>(wlan_ieee80211::ReasonCode::TDLS_UNSPECIFIED_REASON):
    case static_cast<T>(wlan_ieee80211::ReasonCode::SSP_REQUESTED_DISASSOC):
    case static_cast<T>(wlan_ieee80211::ReasonCode::NO_SSP_ROAMING_AGREEMENT):
    case static_cast<T>(wlan_ieee80211::ReasonCode::BAD_CIPHER_OR_AKM):
    case static_cast<T>(wlan_ieee80211::ReasonCode::NOT_AUTHORIZED_THIS_LOCATION):
    case static_cast<T>(wlan_ieee80211::ReasonCode::SERVICE_CHANGE_PRECLUDES_TS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::UNSPECIFIED_QOS_REASON):
    case static_cast<T>(wlan_ieee80211::ReasonCode::NOT_ENOUGH_BANDWIDTH):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MISSING_ACKS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::EXCEEDED_TXOP):
    case static_cast<T>(wlan_ieee80211::ReasonCode::STA_LEAVING):
    case static_cast<T>(wlan_ieee80211::ReasonCode::END_TS_BA_DLS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::UNKNOWN_TS_BA):
    case static_cast<T>(wlan_ieee80211::ReasonCode::TIMEOUT):
    case static_cast<T>(wlan_ieee80211::ReasonCode::PEERKEY_MISMATCH):
    case static_cast<T>(wlan_ieee80211::ReasonCode::PEER_INITIATED):
    case static_cast<T>(wlan_ieee80211::ReasonCode::AP_INITIATED):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INVALID_FT_ACTION_FRAME_COUNT):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INVALID_PMKID):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INVALID_MDE):
    case static_cast<T>(wlan_ieee80211::ReasonCode::REASON_INVALID_FTE):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_PEERING_CANCELED):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_MAX_PEERS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_CONFIGURATION_POLICY_VIOLATION):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_CLOSE_RCVD):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_MAX_RETRIES):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_CONFIRM_TIMEOUT):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_INVALID_GTK):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_INCONSISTENT_PARAMETERS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_INVALID_SECURITY_CAPABILITY):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_PATH_ERROR_NO_PROXY_INFORMATION):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_PATH_ERROR_NO_FORWARDING_INFORMATION):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_PATH_ERROR_DESTINATION_UNREACHABLE):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MAC_ADDRESS_ALREADY_EXISTS_IN_MBSS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_CHANNEL_SWITCH_REGULATORY_REQUIREMENTS):
    case static_cast<T>(wlan_ieee80211::ReasonCode::MESH_CHANNEL_SWITCH_UNSPECIFIED):
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

uint16_t ConvertReasonCode(wlan_ieee80211::ReasonCode reason) {
  ZX_ASSERT(IsValidReasonCode(reason));
  return static_cast<uint16_t>(reason);
}

uint16_t ConvertStatusCode(wlan_ieee80211::StatusCode status) {
  ZX_ASSERT(IsValidStatusCode(status));
  return static_cast<uint16_t>(status);
}

wlan_ieee80211::ReasonCode ConvertReasonCode(uint16_t reason) {
  // Use a default for invalid uint16_t reason codes from external sources.
  if (!IsValidReasonCode(reason)) {
    return wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON;
  }
  return static_cast<wlan_ieee80211::ReasonCode>(reason);
}

wlan_ieee80211::StatusCode ConvertStatusCode(uint16_t status) {
  // Use a default for invalid uint16_t status codes from external sources.
  if (!IsValidStatusCode(status)) {
    return wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED;
  }
  return static_cast<wlan_ieee80211::StatusCode>(status);
}

}  // namespace common
}  // namespace wlan
