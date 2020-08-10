// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <wlan/common/mac_frame.h>
#include <wlan/mlme/convert.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

wlan_status_code ToStatusCode(const wlan_mlme::AuthenticateResultCodes code) {
  switch (code) {
    case wlan_mlme::AuthenticateResultCodes::SUCCESS:
      return WLAN_STATUS_CODE_SUCCESS;
    case wlan_mlme::AuthenticateResultCodes::REFUSED:
      return WLAN_STATUS_CODE_REFUSED;
    case wlan_mlme::AuthenticateResultCodes::ANTI_CLOGGING_TOKEN_REQUIRED:
      return WLAN_STATUS_CODE_ANTI_CLOGGING_TOKEN_REQUIRED;
    case wlan_mlme::AuthenticateResultCodes::FINITE_CYCLIC_GROUP_NOT_SUPPORTED:
      return WLAN_STATUS_CODE_UNSUPPORTED_FINITE_CYCLIC_GROUP;
    case wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED:
      return WLAN_STATUS_CODE_UNSUPPORTED_AUTH_ALGORITHM;
    case wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT:
      return WLAN_STATUS_CODE_REJECTED_SEQUENCE_TIMEOUT;
  }
}

wlan_status_code ToStatusCode(const wlan_mlme::AssociateResultCodes code) {
  switch (code) {
    case wlan_mlme::AssociateResultCodes::SUCCESS:
      return WLAN_STATUS_CODE_SUCCESS;
    case wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED:
      return WLAN_STATUS_CODE_REFUSED;
    case wlan_mlme::AssociateResultCodes::REFUSED_NOT_AUTHENTICATED:
      return WLAN_STATUS_CODE_REFUSED_UNAUTHENTICATED_ACCESS_NOT_SUPPORTED;
    case wlan_mlme::AssociateResultCodes::REFUSED_CAPABILITIES_MISMATCH:
      return WLAN_STATUS_CODE_REFUSED_CAPABILITIES_MISMATCH;
    case wlan_mlme::AssociateResultCodes::REFUSED_EXTERNAL_REASON:
      return WLAN_STATUS_CODE_REFUSED_EXTERNAL_REASON;
    case wlan_mlme::AssociateResultCodes::REFUSED_AP_OUT_OF_MEMORY:
      return WLAN_STATUS_CODE_REFUSED_AP_OUT_OF_MEMORY;
    case wlan_mlme::AssociateResultCodes::REFUSED_BASIC_RATES_MISMATCH:
      return WLAN_STATUS_CODE_REFUSED_BASIC_RATES_MISMATCH;
    case wlan_mlme::AssociateResultCodes::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED:
      return WLAN_STATUS_CODE_REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED;
    case wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY:
      return WLAN_STATUS_CODE_REFUSED_TEMPORARILY;
  }
}

}  // namespace wlan
