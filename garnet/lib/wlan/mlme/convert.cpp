// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/mac_frame.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

status_code::StatusCode ToStatusCode(const wlan_mlme::AuthenticateResultCodes code) {
    switch (code) {
    case wlan_mlme::AuthenticateResultCodes::SUCCESS:
        return status_code::StatusCode::kSuccess;
    case wlan_mlme::AuthenticateResultCodes::REFUSED:
        return status_code::StatusCode::kRefused;
    case wlan_mlme::AuthenticateResultCodes::ANTI_CLOGGING_TOKEN_REQUIRED:
        return status_code::StatusCode::kAntiCloggingTokenRequired;
    case wlan_mlme::AuthenticateResultCodes::FINITE_CYCLIC_GROUP_NOT_SUPPORTED:
        return status_code::StatusCode::kUnsupportedFiniteCyclicGroup;
    case wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED:
        return status_code::StatusCode::kUnsupportedAuthAlgorithm;
    case wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT:
        return status_code::StatusCode::kRejectedSequenceTimeout;
    }
}

status_code::StatusCode ToStatusCode(const wlan_mlme::AssociateResultCodes code) {
    switch (code) {
    case wlan_mlme::AssociateResultCodes::SUCCESS:
        return status_code::StatusCode::kSuccess;
    case wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED:
        return status_code::StatusCode::kRefusedReasonUnspecified;
    case wlan_mlme::AssociateResultCodes::REFUSED_NOT_AUTHENTICATED:
        return status_code::StatusCode::kRefusedUnauthenticatedAccessNotSupported;
    case wlan_mlme::AssociateResultCodes::REFUSED_CAPABILITIES_MISMATCH:
        return status_code::StatusCode::kRefusedCapabilitiesMismatch;
    case wlan_mlme::AssociateResultCodes::REFUSED_EXTERNAL_REASON:
        return status_code::StatusCode::kRefusedExternalReason;
    case wlan_mlme::AssociateResultCodes::REFUSED_AP_OUT_OF_MEMORY:
        return status_code::StatusCode::kRefusedApOutOfMemory;
    case wlan_mlme::AssociateResultCodes::REFUSED_BASIC_RATES_MISMATCH:
        return status_code::StatusCode::kRefusedBasicRatesMismatch;
    case wlan_mlme::AssociateResultCodes::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED:
        return status_code::StatusCode::kRejectedEmergencyServicesNotSupported;
    case wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY:
        return status_code::StatusCode::kRefusedTemporarily;
    }
}

}  // namespace wlan