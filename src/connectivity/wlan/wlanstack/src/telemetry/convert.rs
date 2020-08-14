// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    log::warn,
    wlan_common::bss::Protection,
    wlan_metrics_registry as metrics,
    wlan_sme::client::{
        info::{ConnectionMilestone, DisconnectCause, ScanResult},
        ConnectFailure, ConnectResult, EstablishRsnaFailure, SelectNetworkFailure,
    },
};

pub(super) fn convert_connect_failure(
    result: &ConnectFailure,
) -> Option<metrics::ConnectionDelayMetricDimensionConnectionResult> {
    use fidl_mlme::AssociateResultCodes as AssocCodes;
    use fidl_mlme::AuthenticateResultCodes as AuthCodes;
    use metrics::ConnectionDelayMetricDimensionConnectionResult::*;

    let result = match result {
        ConnectFailure::SelectNetwork(failure) => match failure {
            SelectNetworkFailure::NoScanResultWithSsid
            | SelectNetworkFailure::NoCompatibleNetwork => NoMatchingBssFound,
            _ => Fail,
        },
        ConnectFailure::ScanFailure(scan_failure) => match scan_failure {
            fidl_mlme::ScanResultCodes::Success => return None,
            fidl_mlme::ScanResultCodes::NotSupported => ScanNotSupported,
            fidl_mlme::ScanResultCodes::InvalidArgs => ScanInvalidArgs,
            fidl_mlme::ScanResultCodes::InternalError => ScanInternalError,
        },
        ConnectFailure::JoinFailure(join_failure) => match join_failure {
            fidl_mlme::JoinResultCodes::Success => return None,
            fidl_mlme::JoinResultCodes::JoinFailureTimeout => JoinFailureTimeout,
        },
        ConnectFailure::AuthenticationFailure(auth_failure) => match auth_failure {
            AuthCodes::Success => return None,
            AuthCodes::Refused => AuthenticationRefused,
            AuthCodes::AntiCloggingTokenRequired => AuthenticationAntiCloggingTokenRequired,
            AuthCodes::FiniteCyclicGroupNotSupported => AuthenticationFiniteCyclicGroupNotSupported,
            AuthCodes::AuthenticationRejected => AuthenticationRejected,
            AuthCodes::AuthFailureTimeout => AuthenticationFailureTimeout,
        },
        ConnectFailure::AssociationFailure(assoc_failure) => match assoc_failure {
            AssocCodes::Success => return None,
            AssocCodes::RefusedReasonUnspecified => AssociationRefusedReasonUnspecified,
            AssocCodes::RefusedNotAuthenticated => AssociationRefusedNotAuthenticated,
            AssocCodes::RefusedCapabilitiesMismatch => AssociationRefusedCapabilitiesMismatch,
            AssocCodes::RefusedExternalReason => AssociationRefusedExternalReason,
            AssocCodes::RefusedApOutOfMemory => AssociationRefusedApOutOfMemory,
            AssocCodes::RefusedBasicRatesMismatch => AssociationRefusedBasicRatesMismatch,
            AssocCodes::RejectedEmergencyServicesNotSupported => {
                AssociationRejectedEmergencyServicesNotSupported
            }
            AssocCodes::RefusedTemporarily => AssociationRefusedTemporarily,
        },
        ConnectFailure::EstablishRsna(rsna_failure) => match rsna_failure {
            EstablishRsnaFailure::OverallTimeout
            | EstablishRsnaFailure::KeyFrameExchangeTimeout => RsnaTimeout,
            _ => Fail,
        },
    };

    Some(result)
}

// Multiple metrics' channel band dimensions are the same, so we use the enum from one metric to
// represent all of them.
//
// The same applies to other methods below that may return dimension used by multiple metrics.
pub(super) fn convert_channel_band(
    channel: u8,
) -> metrics::ConnectionSuccessWithAttemptsBreakdownMetricDimensionChannelBand {
    use metrics::ConnectionSuccessWithAttemptsBreakdownMetricDimensionChannelBand::*;
    if channel > 14 {
        Band5Ghz
    } else {
        Band2Dot4Ghz
    }
}

pub(super) fn convert_connection_result(
    result: &ConnectResult,
) -> metrics::ConnectionResultMetricDimensionResult {
    use metrics::ConnectionResultMetricDimensionResult::*;
    match result {
        ConnectResult::Success => Success,
        ConnectResult::Canceled => Canceled,
        ConnectResult::Failed(..) => Failed,
    }
}

pub(super) fn convert_protection(
    protection: &Protection,
) -> metrics::ConnectionSuccessWithAttemptsBreakdownMetricDimensionProtection {
    use metrics::ConnectionSuccessWithAttemptsBreakdownMetricDimensionProtection::*;
    match protection {
        Protection::Unknown => Unknown,
        Protection::Open => Open,
        Protection::Wep => Wep,
        Protection::Wpa1 => Wpa1,
        Protection::Wpa1Wpa2Personal => Wpa1Wpa2Personal,
        Protection::Wpa2Personal => Wpa2Personal,
        Protection::Wpa2Wpa3Personal => Wpa2Wpa3Personal,
        Protection::Wpa3Personal => Wpa3Personal,
        Protection::Wpa2Enterprise => Wpa2Enterprise,
        Protection::Wpa3Enterprise => Wpa3Enterprise,
    }
}

pub(super) fn convert_rssi(rssi: i8) -> metrics::ConnectionResultPerRssiMetricDimensionRssi {
    use metrics::ConnectionResultPerRssiMetricDimensionRssi::*;
    match (rssi as i16).abs() {
        // TODO(35522) Change From127To90 to From128To90 in Cobalt so that they are consistent
        90..=128 => From127To90,
        86..=89 => From89To86,
        83..=85 => From85To83,
        80..=82 => From82To80,
        77..=79 => From79To77,
        74..=76 => From76To74,
        71..=73 => From73To71,
        66..=70 => From70To66,
        61..=65 => From65To61,
        51..=60 => From60To51,
        1..=50 => From50To1,
        _ => _0,
    }
}

pub(super) fn convert_bool_dim(
    value: bool,
) -> metrics::ConnectionSuccessWithAttemptsBreakdownMetricDimensionIsMultiBss {
    use metrics::ConnectionSuccessWithAttemptsBreakdownMetricDimensionIsMultiBss::*;
    match value {
        true => Yes,
        false => No,
    }
}

pub(super) fn convert_to_fail_at_dim(
    failure: &ConnectFailure,
) -> metrics::ConnectionFailureMetricDimensionFailAt {
    use metrics::ConnectionFailureMetricDimensionFailAt::*;
    match failure {
        ConnectFailure::ScanFailure(..) => Scan,
        ConnectFailure::SelectNetwork(..) => NetworkSelection,
        ConnectFailure::JoinFailure(..) => Join,
        ConnectFailure::AuthenticationFailure(..) => Authentication,
        ConnectFailure::AssociationFailure(..) => Association,
        ConnectFailure::EstablishRsna(..) => EstablishRsna,
    }
}

pub(super) fn convert_auth_error_code(
    code: fidl_mlme::AuthenticateResultCodes,
) -> metrics::AuthenticationFailureMetricDimensionErrorCode {
    use metrics::AuthenticationFailureMetricDimensionErrorCode::*;
    match code {
        fidl_mlme::AuthenticateResultCodes::Success => {
            warn!("unexpected success code in auth failure");
            Refused
        }
        fidl_mlme::AuthenticateResultCodes::Refused => Refused,
        fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired => AntiCloggingTokenRequired,
        fidl_mlme::AuthenticateResultCodes::FiniteCyclicGroupNotSupported => {
            FiniteCyclicGroupNotSupported
        }
        fidl_mlme::AuthenticateResultCodes::AuthenticationRejected => AuthenticationRejected,
        fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout => AuthFailureTimeout,
    }
}

pub(super) fn convert_assoc_error_code(
    code: fidl_mlme::AssociateResultCodes,
) -> metrics::AssociationFailureMetricDimensionErrorCode {
    use metrics::AssociationFailureMetricDimensionErrorCode::*;
    match code {
        fidl_mlme::AssociateResultCodes::Success => {
            warn!("unexpected success code in assoc failure");
            RefusedReasonUnspecified
        }
        fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified => RefusedReasonUnspecified,
        fidl_mlme::AssociateResultCodes::RefusedNotAuthenticated => RefusedNotAuthenticated,
        fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch => RefusedCapabilitiesMismatch,
        fidl_mlme::AssociateResultCodes::RefusedExternalReason => RefusedExternalReason,
        fidl_mlme::AssociateResultCodes::RefusedApOutOfMemory => RefusedApOutOfMemory,
        fidl_mlme::AssociateResultCodes::RefusedBasicRatesMismatch => RefusedBasicRatesMismatch,
        fidl_mlme::AssociateResultCodes::RejectedEmergencyServicesNotSupported => {
            RejectedEmergencyServicesNotSupported
        }
        fidl_mlme::AssociateResultCodes::RefusedTemporarily => RefusedTemporarily,
    }
}

pub(super) fn convert_scan_result(
    result: &ScanResult,
) -> (
    metrics::ScanResultMetricDimensionScanResult,
    Option<metrics::ScanFailureMetricDimensionErrorCode>,
) {
    use metrics::ScanFailureMetricDimensionErrorCode::*;
    use metrics::ScanResultMetricDimensionScanResult::*;

    match result {
        ScanResult::Success => (Success, None),
        ScanResult::Failed(error_code) => (
            Failed,
            Some(match error_code {
                fidl_mlme::ScanResultCodes::NotSupported => NotSupported,
                fidl_mlme::ScanResultCodes::InvalidArgs => InvalidArgs,
                fidl_mlme::ScanResultCodes::InternalError => InternalError,
                // This shouldn't happen, but we'll just map it to InternalError
                fidl_mlme::ScanResultCodes::Success => InternalError,
            }),
        ),
    }
}

pub(super) fn convert_scan_type(
    scan_type: fidl_mlme::ScanTypes,
) -> metrics::ScanResultMetricDimensionScanType {
    match scan_type {
        fidl_mlme::ScanTypes::Active => metrics::ScanResultMetricDimensionScanType::Active,
        fidl_mlme::ScanTypes::Passive => metrics::ScanResultMetricDimensionScanType::Passive,
    }
}

pub(super) fn convert_select_network_failure(
    failure: &SelectNetworkFailure,
) -> metrics::NetworkSelectionFailureMetricDimensionErrorReason {
    use metrics::NetworkSelectionFailureMetricDimensionErrorReason::*;
    match failure {
        SelectNetworkFailure::NoScanResultWithSsid => NoScanResultWithSsid,
        SelectNetworkFailure::NoCompatibleNetwork => NoCompatibleNetwork,
        SelectNetworkFailure::CredentialError(_) => CredentialError,
    }
}

pub(super) fn convert_disconnect_cause(
    disconnect_cause: &DisconnectCause,
) -> metrics::ConnectionGapTimeBreakdownMetricDimensionPreviousDisconnectCause {
    use metrics::ConnectionGapTimeBreakdownMetricDimensionPreviousDisconnectCause::*;
    match disconnect_cause {
        DisconnectCause::Manual => Manual,
        DisconnectCause::Drop => Drop,
    }
}

pub(super) fn convert_connected_milestone(
    milestone: &ConnectionMilestone,
) -> metrics::ConnectionCountByDurationMetricDimensionConnectedTime {
    use metrics::ConnectionCountByDurationMetricDimensionConnectedTime::*;
    match milestone {
        ConnectionMilestone::Connected => Connected,
        ConnectionMilestone::OneMinute => ConnectedOneMinute,
        ConnectionMilestone::TenMinutes => ConnectedTenMinute,
        ConnectionMilestone::ThirtyMinutes => ConnectedThirtyMinute,
        ConnectionMilestone::OneHour => ConnectedOneHour,
        ConnectionMilestone::ThreeHours => ConnectedThreeHours,
        ConnectionMilestone::SixHours => ConnectedSixHours,
        ConnectionMilestone::TwelveHours => ConnectedTwelveHours,
        ConnectionMilestone::OneDay => ConnectedOneDay,
        ConnectionMilestone::TwoDays => ConnectedTwoDays,
        ConnectionMilestone::ThreeDays => ConnectedThreeDays,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn convert_rssi_must_not_overflow() {
        use metrics::ConnectionResultPerRssiMetricDimensionRssi::*;
        assert_eq!(convert_rssi(-128), From127To90);
        assert_eq!(convert_rssi(-127), From127To90);
        assert_eq!(convert_rssi(-1), From50To1);
        assert_eq!(convert_rssi(0), _0);
    }
}
