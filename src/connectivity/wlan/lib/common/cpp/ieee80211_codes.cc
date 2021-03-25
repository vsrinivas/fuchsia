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

uint16_t ConvertReasonCode(wlan_ieee80211::ReasonCode reason_code) {
  // For every RESERVED_X_TO_Y value, this will return X.
  return static_cast<uint16_t>(reason_code);
}

uint16_t ConvertStatusCode(wlan_ieee80211::StatusCode status) {
  ZX_ASSERT(IsValidStatusCode(status));
  return static_cast<uint16_t>(status);
}

wlan_ieee80211::ReasonCode ConvertReasonCode(uint16_t reason_code) {
  if (0 == reason_code) {
    return wlan_ieee80211::ReasonCode::RESERVED_0;
  }
  if (67 <= reason_code && reason_code <= 127) {
    return wlan_ieee80211::ReasonCode::RESERVED_67_TO_127;
  }
  if (130 <= reason_code && reason_code <= UINT16_MAX) {
    return wlan_ieee80211::ReasonCode::RESERVED_130_TO_65535;
  }
  return static_cast<wlan_ieee80211::ReasonCode>(reason_code);
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
