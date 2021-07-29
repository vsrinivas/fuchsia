// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    log::warn,
    wlan_common::bss::Protection,
    wlan_metrics_registry as metrics,
    wlan_sme::client::{
        info::{ConnectionMilestone, DisconnectSource},
        ConnectFailure, ConnectResult, SelectNetworkFailure,
    },
};

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
        Protection::Wpa1Wpa2PersonalTkipOnly => Wpa1Wpa2PersonalTkipOnly,
        Protection::Wpa2PersonalTkipOnly => Wpa2PersonalTkipOnly,
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
        // TODO(fxbug.dev/35522) Change From127To90 to From128To90 in Cobalt so that they are consistent
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

pub(super) fn convert_snr(snr: i8) -> metrics::ConnectionResultPerSnrMetricDimensionSnr {
    use metrics::ConnectionResultPerSnrMetricDimensionSnr::*;
    match snr {
        1..=10 => From1To10,
        11..=15 => From11To15,
        16..=25 => From16To25,
        26..=40 => From26To40,
        41..=127 => MoreThan40,
        _ => _0,
    }
}

// All Yes/No metric dimensions are aliased to
// metrics::ConnectionSuccessWithAttemptsBreakdownMetricDimensionIsMultiBss
// in the auto-generation of the metrics crate. This function uses the
// source of the aliases rather than each alias itself since it does
// not make a difference otherwise.
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
        ConnectFailure::SelectNetworkFailure(..) => NetworkSelection,
        ConnectFailure::JoinFailure(..) => Join,
        ConnectFailure::AuthenticationFailure(..) => Authentication,
        ConnectFailure::AssociationFailure(..) => Association,
        ConnectFailure::EstablishRsnaFailure(..) => EstablishRsna,
    }
}

pub(super) fn convert_auth_error_code(
    code: fidl_mlme::AuthenticateResultCode,
) -> metrics::AuthenticationFailureMetricDimensionErrorCode {
    use metrics::AuthenticationFailureMetricDimensionErrorCode::*;
    match code {
        fidl_mlme::AuthenticateResultCode::Success => {
            warn!("unexpected success code in auth failure");
            Refused
        }
        fidl_mlme::AuthenticateResultCode::Refused => Refused,
        fidl_mlme::AuthenticateResultCode::AntiCloggingTokenRequired => AntiCloggingTokenRequired,
        fidl_mlme::AuthenticateResultCode::FiniteCyclicGroupNotSupported => {
            FiniteCyclicGroupNotSupported
        }
        fidl_mlme::AuthenticateResultCode::AuthenticationRejected => AuthenticationRejected,
        fidl_mlme::AuthenticateResultCode::AuthFailureTimeout => AuthFailureTimeout,
    }
}

pub(super) fn convert_assoc_error_code(
    code: fidl_mlme::AssociateResultCode,
) -> metrics::AssociationFailureMetricDimensionErrorCode {
    use metrics::AssociationFailureMetricDimensionErrorCode::*;
    match code {
        fidl_mlme::AssociateResultCode::Success => {
            warn!("unexpected success code in assoc failure");
            RefusedReasonUnspecified
        }
        fidl_mlme::AssociateResultCode::RefusedReasonUnspecified => RefusedReasonUnspecified,
        fidl_mlme::AssociateResultCode::RefusedNotAuthenticated => RefusedNotAuthenticated,
        fidl_mlme::AssociateResultCode::RefusedCapabilitiesMismatch => RefusedCapabilitiesMismatch,
        fidl_mlme::AssociateResultCode::RefusedExternalReason => RefusedExternalReason,
        fidl_mlme::AssociateResultCode::RefusedApOutOfMemory => RefusedApOutOfMemory,
        fidl_mlme::AssociateResultCode::RefusedBasicRatesMismatch => RefusedBasicRatesMismatch,
        fidl_mlme::AssociateResultCode::RejectedEmergencyServicesNotSupported => {
            RejectedEmergencyServicesNotSupported
        }
        fidl_mlme::AssociateResultCode::RefusedTemporarily => RefusedTemporarily,
    }
}

pub(super) fn convert_scan_result(
    result_code: &fidl_mlme::ScanResultCode,
) -> (
    metrics::ScanResultMetricDimensionScanResult,
    Option<metrics::ScanFailureMetricDimensionErrorCode>,
) {
    use metrics::ScanFailureMetricDimensionErrorCode::*;
    use metrics::ScanResultMetricDimensionScanResult::*;

    match result_code {
        fidl_mlme::ScanResultCode::Success => None,
        fidl_mlme::ScanResultCode::NotSupported => Some(NotSupported),
        fidl_mlme::ScanResultCode::InvalidArgs => Some(InvalidArgs),
        fidl_mlme::ScanResultCode::InternalError => Some(InternalError),
        fidl_mlme::ScanResultCode::ShouldWait => Some(InternalError),
        fidl_mlme::ScanResultCode::CanceledByDriverOrFirmware => Some(InternalError),
    }
    .map_or((Success, None), |error_code_dim| (Failed, Some(error_code_dim)))
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
        SelectNetworkFailure::IncompatibleConnectRequest => IncompatibleConnectRequest,
        SelectNetworkFailure::InternalProtectionError => InternalProtectionError,
    }
}

pub(super) fn convert_disconnect_source(
    disconnect_source: &DisconnectSource,
) -> metrics::ConnectionGapTimeBreakdownMetricDimensionPreviousDisconnectCause {
    use metrics::ConnectionGapTimeBreakdownMetricDimensionPreviousDisconnectCause::*;
    match disconnect_source {
        DisconnectSource::User(_) => Manual,
        DisconnectSource::Mlme(_) | DisconnectSource::Ap(_) => Drop,
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

    #[test]
    fn convert_snr_must_not_underflow() {
        use metrics::ConnectionResultPerSnrMetricDimensionSnr::*;
        assert_eq!(convert_snr(127), MoreThan40);
        assert_eq!(convert_snr(28), From26To40);
        assert_eq!(convert_snr(1), From1To10);
        assert_eq!(convert_snr(0), _0);
        assert_eq!(convert_snr(-1), _0);
        assert_eq!(convert_snr(-128), _0);
    }
}
