// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {at_commands as at, core::fmt::Debug, tracing::warn};

use crate::peer::{
    calls::types::{Call, CallHeld, CallSetup},
    update::AgUpdate,
};

/// This implementation supports the 7 indicators defined in HFP v1.8 Section 4.35.
/// The indices of these indicators are fixed.
pub(crate) const SERVICE_INDICATOR_INDEX: usize = 1;
pub(crate) const CALL_INDICATOR_INDEX: usize = 2;
pub(crate) const CALL_SETUP_INDICATOR_INDEX: usize = 3;
pub(crate) const CALL_HELD_INDICATOR_INDEX: usize = 4;
pub(crate) const SIGNAL_INDICATOR_INDEX: usize = 5;
pub(crate) const ROAM_INDICATOR_INDEX: usize = 6;
pub(crate) const BATT_CHG_INDICATOR_INDEX: usize = 7;

/// The supported HF indicators.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum HfIndicator {
    EnhancedSafety(bool),
    BatteryLevel(u8),
}

/// A single indicator status + value.
#[derive(Clone, Copy, Debug)]
struct Indicator<T: Clone + Copy + Debug> {
    /// Whether this indicator is enabled or not.
    enabled: bool,
    /// The value of the indicator.
    value: Option<T>,
}

impl<T: Clone + Copy + Debug> Default for Indicator<T> {
    fn default() -> Self {
        Self { enabled: false, value: None }
    }
}

/// The supported HF indicators and their enabled/disabled status & values.
/// Defined in HFP v1.8 Section 4.36.
#[derive(Clone, Copy, Debug, Default)]
pub struct HfIndicators {
    /// The Enhanced Safety HF indicator. There are only two potential values (enabled, disabled).
    enhanced_safety: Indicator<bool>,
    /// The Battery Level HF indicator. Can be any integer value between [0, 100].
    battery_level: Indicator<u8>,
}

impl HfIndicators {
    /// The Maximum Battery Level value for the `battery_level` indicator.
    /// Defined in HFP v1.8 Section 4.35.
    const MAX_BATTERY_LEVEL: u8 = 100;

    #[cfg(test)]
    pub fn enhanced_safety_enabled(&self) -> bool {
        self.enhanced_safety.enabled
    }

    #[cfg(test)]
    pub fn battery_level_enabled(&self) -> bool {
        self.battery_level.enabled
    }

    /// Enables the supported HF indicators based on the provided AT `indicators`.
    pub fn enable_indicators(&mut self, indicators: Vec<at::BluetoothHFIndicator>) {
        for ind in indicators {
            if ind == at::BluetoothHFIndicator::EnhancedSafety {
                self.enhanced_safety.enabled = true;
            }
            if ind == at::BluetoothHFIndicator::BatteryLevel {
                self.battery_level.enabled = true;
            }
        }
    }

    /// Updates the `indicator` with the provided `value`.
    /// Returns Error if the indicator is disabled or if the `value` is out of bounds.
    /// Returns a valid HfIndicator on success.
    pub fn update_indicator_value(
        &mut self,
        indicator: at::BluetoothHFIndicator,
        value: i64,
    ) -> Result<HfIndicator, ()> {
        let ind = match indicator {
            at::BluetoothHFIndicator::EnhancedSafety if self.enhanced_safety.enabled => {
                if value != 0 && value != 1 {
                    return Err(());
                }
                let v = value != 0;
                self.enhanced_safety.value = Some(v);
                HfIndicator::EnhancedSafety(v)
            }
            at::BluetoothHFIndicator::BatteryLevel if self.battery_level.enabled => {
                if value < 0 || value > Self::MAX_BATTERY_LEVEL.into() {
                    return Err(());
                }
                let v = value as u8;
                self.battery_level.value = Some(v);
                HfIndicator::BatteryLevel(v)
            }
            ind => {
                warn!("Received HF indicator update for disabled indicator: {:?}", ind);
                return Err(());
            }
        };
        Ok(ind)
    }

    /// Returns the +BIND response for the current HF indicator status.
    pub fn bind_response(&self) -> Vec<at::Response> {
        vec![
            at::success(at::Success::BindStatus {
                anum: at::BluetoothHFIndicator::EnhancedSafety,
                state: self.enhanced_safety.enabled,
            }),
            at::success(at::Success::BindStatus {
                anum: at::BluetoothHFIndicator::BatteryLevel,
                state: self.battery_level.enabled,
            }),
            at::Response::Ok,
        ]
    }
}

/// A collection of indicators supported by the AG.
#[derive(Debug, Default, Clone, Copy)]
pub struct AgIndicators {
    pub service: bool,
    pub call: Call,
    pub callsetup: CallSetup,
    pub callheld: CallHeld,
    pub signal: u8,
    pub roam: bool,
    pub battchg: u8,
}

/// The supported phone status update indicators.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum AgIndicator {
    Service(u8),
    Call(u8),
    CallSetup(u8),
    CallHeld(u8),
    Signal(u8),
    Roam(u8),
    BatteryLevel(u8),
}

impl From<AgIndicator> for at::Response {
    fn from(src: AgIndicator) -> at::Response {
        match src {
            AgIndicator::Service(v) => at::Response::Success(at::Success::Ciev {
                ind: SERVICE_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            AgIndicator::Call(v) => at::Response::Success(at::Success::Ciev {
                ind: CALL_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            AgIndicator::CallSetup(v) => at::Response::Success(at::Success::Ciev {
                ind: CALL_SETUP_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            AgIndicator::CallHeld(v) => at::Response::Success(at::Success::Ciev {
                ind: CALL_HELD_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            AgIndicator::Signal(v) => at::Response::Success(at::Success::Ciev {
                ind: SIGNAL_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            AgIndicator::Roam(v) => at::Response::Success(at::Success::Ciev {
                ind: ROAM_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            AgIndicator::BatteryLevel(v) => at::Response::Success(at::Success::Ciev {
                ind: BATT_CHG_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
        }
    }
}

impl From<Call> for AgIndicator {
    fn from(src: Call) -> Self {
        Self::Call(src as u8)
    }
}

impl From<CallSetup> for AgIndicator {
    fn from(src: CallSetup) -> Self {
        Self::CallSetup(src as u8)
    }
}

impl From<CallHeld> for AgIndicator {
    fn from(src: CallHeld) -> Self {
        Self::CallHeld(src as u8)
    }
}

impl From<AgIndicator> for AgUpdate {
    fn from(src: AgIndicator) -> Self {
        Self::PhoneStatusIndicator(src)
    }
}

/// The current AG Indicators Reporting status with the active/inactive status of each
/// supported indicator.
///
/// Per HFP v1.8 Section 4.35, the Call, Call Setup, and Call Held indicators
/// must always be activated; these fields are read-only and will always
/// be set and therefore are omitted from the `AgIndicatorsReporting` object.
///
/// It is valid to toggle the activeness of specific event even if Indicators Reporting is
/// disabled.
///
/// By default, all indicators are set to enabled.
#[derive(Clone, Debug, PartialEq, Copy)]
pub struct AgIndicatorsReporting {
    is_enabled: bool,

    /// The event indicators.
    service: bool,
    signal: bool,
    roam: bool,
    batt_chg: bool,
}

impl AgIndicatorsReporting {
    /// This mode's behavior is to forward unsolicited result codes directly per
    /// 3GPP TS 27.007 version 6.8.0, Section 8.10.
    /// This is the only supported mode in the Event Reporting Enabling AT command (AT+CMER).
    /// Defined in HFP v1.8 Section 4.34.2.
    pub const EVENT_REPORTING_MODE: i64 = 3;

    /// Enables or disables the indicators reporting state while maintaining current indicator
    /// flags. Valid status values are 0 for disabled and 1 for enabled. Any other value returns an
    /// UnsupportedReportingStatus error. See HFP v1.8 Section 4.34.2 AT+CMER.
    pub fn set_reporting_status(&mut self, status: i64) -> Result<(), UnsupportedReportingStatus> {
        match status {
            0 => self.is_enabled = false,
            1 => self.is_enabled = true,
            _ => return Err(UnsupportedReportingStatus(status)),
        }
        Ok(())
    }

    #[cfg(test)]
    pub fn set_signal(&mut self, toggle: bool) {
        self.signal = toggle;
    }

    #[cfg(test)]
    pub fn set_service(&mut self, toggle: bool) {
        self.service = toggle;
    }

    #[cfg(test)]
    pub fn set_batt_chg(&mut self, toggle: bool) {
        self.batt_chg = toggle;
    }

    #[cfg(test)]
    pub fn new_enabled() -> Self {
        Self { is_enabled: true, service: true, signal: true, roam: true, batt_chg: true }
    }

    pub fn new_disabled() -> Self {
        Self { is_enabled: false, service: true, signal: true, roam: true, batt_chg: true }
    }

    /// Updates the indicators with any indicators specified in `flags`.
    pub fn update_from_flags(&mut self, flags: Vec<Option<bool>>) {
        for (idx, flag) in flags.into_iter().enumerate() {
            // The indicator indices are "1-indexed".
            let index = idx + 1;
            let toggle = if let Some(b) = flag { b } else { continue };
            // See HFP v1.8 Section 4.35 for the flags that cannot be toggled.
            // Any indicators beyond the 7 supported in this implementation can be safely ignored.
            match index {
                SERVICE_INDICATOR_INDEX => self.service = toggle,
                CALL_INDICATOR_INDEX | CALL_SETUP_INDICATOR_INDEX | CALL_HELD_INDICATOR_INDEX => (),
                SIGNAL_INDICATOR_INDEX => self.signal = toggle,
                ROAM_INDICATOR_INDEX => self.roam = toggle,
                BATT_CHG_INDICATOR_INDEX => self.batt_chg = toggle,
                _ => break,
            }
        }
    }

    /// Returns true if indicators reporting is enabled and the provided `status` indicator should
    /// be sent to the peer.
    pub fn indicator_enabled(&self, status: &AgIndicator) -> bool {
        self.is_enabled
            && match status {
                AgIndicator::Service(_) => self.service,
                AgIndicator::Call(_) | AgIndicator::CallSetup(_) | AgIndicator::CallHeld(_) => true,
                AgIndicator::Signal(_) => self.signal,
                AgIndicator::Roam(_) => self.roam,
                AgIndicator::BatteryLevel(_) => self.batt_chg,
            }
    }
}

impl Default for AgIndicatorsReporting {
    /// The default indicators reporting state is disabled.
    /// Per HFP v1.8 Section 4.2.1.3, the HF will _always_ request to enable indicators
    /// reporting in the SLCI procedure (See +CMER AT command).
    fn default() -> Self {
        Self::new_disabled()
    }
}

#[derive(Debug)]
/// An error representing an unsupported reporting status value.
pub struct UnsupportedReportingStatus(i64);

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[fuchsia::test]
    fn update_hf_indicators_with_invalid_values_is_error() {
        let mut hf_indicators = HfIndicators::default();
        hf_indicators.enable_indicators(vec![
            at::BluetoothHFIndicator::BatteryLevel,
            at::BluetoothHFIndicator::EnhancedSafety,
        ]);

        let battery_too_low = -18;
        assert_matches!(
            hf_indicators
                .update_indicator_value(at::BluetoothHFIndicator::BatteryLevel, battery_too_low),
            Err(())
        );
        let battery_too_high = 1243;
        assert_matches!(
            hf_indicators
                .update_indicator_value(at::BluetoothHFIndicator::BatteryLevel, battery_too_high),
            Err(())
        );

        let negative_safety = -1;
        assert_matches!(
            hf_indicators
                .update_indicator_value(at::BluetoothHFIndicator::EnhancedSafety, negative_safety),
            Err(())
        );
        let large_safety = 8;
        assert_matches!(
            hf_indicators
                .update_indicator_value(at::BluetoothHFIndicator::EnhancedSafety, large_safety),
            Err(())
        );
    }

    #[fuchsia::test]
    fn update_disabled_hf_indicators_with_valid_values_is_error() {
        // Default is no indicators set. Therefore any updates are errors.
        let mut hf_indicators = HfIndicators::default();

        let valid_battery = 32;
        assert_matches!(
            hf_indicators
                .update_indicator_value(at::BluetoothHFIndicator::BatteryLevel, valid_battery),
            Err(())
        );

        let valid_safety = 0;
        assert_matches!(
            hf_indicators
                .update_indicator_value(at::BluetoothHFIndicator::EnhancedSafety, valid_safety),
            Err(())
        );
    }

    #[fuchsia::test]
    fn update_hf_indicators_with_valid_values_is_ok() {
        let mut hf_indicators = HfIndicators::default();
        // Default values.
        assert_eq!(hf_indicators.enhanced_safety.value, None);
        assert_eq!(hf_indicators.enhanced_safety.enabled, false);
        assert_eq!(hf_indicators.battery_level.value, None);
        assert_eq!(hf_indicators.battery_level.enabled, false);

        // Enable both.
        hf_indicators.enable_indicators(vec![
            at::BluetoothHFIndicator::BatteryLevel,
            at::BluetoothHFIndicator::EnhancedSafety,
        ]);

        let valid_battery = 83;
        assert_matches!(
            hf_indicators
                .update_indicator_value(at::BluetoothHFIndicator::BatteryLevel, valid_battery),
            Ok(HfIndicator::BatteryLevel(83))
        );
        assert_eq!(hf_indicators.battery_level.value, Some(valid_battery as u8));

        let valid_safety = 0;
        assert_matches!(
            hf_indicators
                .update_indicator_value(at::BluetoothHFIndicator::EnhancedSafety, valid_safety),
            Ok(HfIndicator::EnhancedSafety(false))
        );
        assert_eq!(hf_indicators.enhanced_safety.value, Some(false));
    }

    #[fuchsia::test]
    fn default_indicators_reporting_is_disabled_with_all_indicators_enabled() {
        let default = AgIndicatorsReporting::default();
        assert!(!default.is_enabled);
        assert!(default.service);
        assert!(default.signal);
        assert!(default.roam);
        assert!(default.batt_chg);
    }

    #[fuchsia::test]
    fn indicator_flags_are_updated_from_bool_flags() {
        // No flags is OK, no updates.
        let mut status = AgIndicatorsReporting::default();
        let empty = vec![];
        let expected = status.clone();
        status.update_from_flags(empty);
        assert_eq!(status, expected);

        // An incomplete set of flags is OK (5 out of 7 supplied). The Call, Call Held, Call Setup
        // flags will not be overridden.
        let mut status = AgIndicatorsReporting::default();
        let incomplete = vec![Some(false), None, Some(false), Some(true)];
        let expected = AgIndicatorsReporting {
            is_enabled: false,
            service: false,
            signal: true,
            roam: true,
            batt_chg: true,
        };
        status.update_from_flags(incomplete);
        assert_eq!(status, expected);

        // A typical set of flags (all 7) is OK.
        let mut status = AgIndicatorsReporting::default();
        let flags =
            vec![None, Some(false), Some(false), Some(true), Some(false), Some(false), None];
        let expected = AgIndicatorsReporting {
            is_enabled: false,
            service: true,
            signal: false,
            roam: false,
            batt_chg: true,
        };
        status.update_from_flags(flags);
        assert_eq!(status, expected);

        // Too many flags provided is also OK. Per the spec, this can happen, and the excess flags
        // are gracefully ignored.
        let mut status = AgIndicatorsReporting::default();
        let too_many =
            vec![None, None, None, None, Some(true), Some(false), None, Some(true), Some(false)];
        let expected = AgIndicatorsReporting {
            is_enabled: false,
            service: true,
            signal: true,
            roam: false,
            batt_chg: true,
        };
        status.update_from_flags(too_many);
        assert_eq!(status, expected);
    }

    #[fuchsia::test]
    fn toggling_indicators_reporting_maintains_same_indicators() {
        let mut status = AgIndicatorsReporting::default();
        assert!(!status.is_enabled);

        // Unset a specific indicator.
        status.set_batt_chg(false);

        // Toggling indicators reporting should preserve indicator values.
        let expected1 = AgIndicatorsReporting { is_enabled: false, ..status.clone() };
        status.set_reporting_status(0).unwrap();
        assert_eq!(status, expected1);
        status.set_reporting_status(0).unwrap();
        assert_eq!(status, expected1);

        status.set_reporting_status(1).unwrap();
        let expected2 = AgIndicatorsReporting { is_enabled: true, ..expected1.clone() };
        assert_eq!(status, expected2);
        assert!(status.is_enabled);
    }

    #[fuchsia::test]
    fn indicator_enabled_is_false_when_indicators_reporting_disabled() {
        let status = AgIndicatorsReporting::new_disabled();

        // Even though all the individual indicator values are toggled on, we expect
        // `indicator_enabled` to return false because indicators reporting is disabled.
        assert!(!status.indicator_enabled(&AgIndicator::Service(0)));
        assert!(!status.indicator_enabled(&AgIndicator::Call(0)));
    }

    #[fuchsia::test]
    fn indicator_enabled_check_returns_expected_result() {
        let mut status = AgIndicatorsReporting::new_enabled();
        status.batt_chg = false;
        status.roam = false;

        assert!(status.indicator_enabled(&AgIndicator::Service(0)));
        assert!(status.indicator_enabled(&AgIndicator::Signal(0)));
        assert!(status.indicator_enabled(&AgIndicator::Call(0)));
        assert!(status.indicator_enabled(&AgIndicator::CallSetup(0)));
        assert!(status.indicator_enabled(&AgIndicator::CallHeld(0)));
        assert!(!status.indicator_enabled(&AgIndicator::Roam(0)));
        assert!(!status.indicator_enabled(&AgIndicator::BatteryLevel(0)));
    }
}
