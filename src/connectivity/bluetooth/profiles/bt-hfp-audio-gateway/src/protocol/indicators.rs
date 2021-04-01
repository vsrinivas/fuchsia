// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use at_commands as at;

use crate::procedure::AgUpdate;

/// This implementation supports the 7 indicators defined in HFP v1.8 Section 4.35.
/// The indices of these indicators are fixed.
pub(crate) const SERVICE_INDICATOR_INDEX: usize = 1;
pub(crate) const CALL_INDICATOR_INDEX: usize = 2;
pub(crate) const CALL_SETUP_INDICATOR_INDEX: usize = 3;
pub(crate) const CALL_HELD_INDICATOR_INDEX: usize = 4;
pub(crate) const SIGNAL_INDICATOR_INDEX: usize = 5;
pub(crate) const ROAM_INDICATOR_INDEX: usize = 6;
pub(crate) const BATT_CHG_INDICATOR_INDEX: usize = 7;

#[derive(Debug, Default, Clone, Copy)]
pub struct Indicators {
    pub service: bool,
    pub call: bool,
    pub callsetup: (),
    pub callheld: (),
    pub signal: u8,
    pub roam: bool,
    pub battchg: u8,
}

/// The supported phone status update indicators.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Indicator {
    Service(u8),
    Call(u8),
    CallSetup(u8),
    CallHeld(u8),
    Signal(u8),
    Roam(u8),
    BatteryLevel(u8),
}

impl From<Indicator> for at::Response {
    fn from(src: Indicator) -> at::Response {
        match src {
            Indicator::Service(v) => at::Response::Success(at::Success::Ciev {
                ind: SERVICE_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            Indicator::Call(v) => at::Response::Success(at::Success::Ciev {
                ind: CALL_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            Indicator::CallSetup(v) => at::Response::Success(at::Success::Ciev {
                ind: CALL_SETUP_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            Indicator::CallHeld(v) => at::Response::Success(at::Success::Ciev {
                ind: CALL_HELD_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            Indicator::Signal(v) => at::Response::Success(at::Success::Ciev {
                ind: SIGNAL_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            Indicator::Roam(v) => at::Response::Success(at::Success::Ciev {
                ind: ROAM_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
            Indicator::BatteryLevel(v) => at::Response::Success(at::Success::Ciev {
                ind: BATT_CHG_INDICATOR_INDEX as i64,
                value: v as i64,
            }),
        }
    }
}

impl From<Indicator> for AgUpdate {
    fn from(src: Indicator) -> Self {
        Self::PhoneStatusIndicator(src)
    }
}

/// The current Indicators Reporting status with the active/inactive status of each
/// supported indicator.
///
/// Per HFP v1.8 Section 4.35, the Call, Call Setup, and Call Held indicators
/// must always be activated; these fields are read-only and will always
/// be set and therefore are omitted from the `IndicatorsReporting` object.
///
/// It is valid to toggle the activeness of specific event even if Indicators Reporting is
/// disabled.
///
/// By default, all indicators are set to enabled.
#[derive(Clone, Debug, PartialEq)]
pub struct IndicatorsReporting {
    is_enabled: bool,

    /// The event indicators.
    service: bool,
    signal: bool,
    roam: bool,
    batt_chg: bool,
}

impl IndicatorsReporting {
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

    /// Sets the indicators reporting state to enabled while maintaining current indicator
    /// flags.
    pub fn enable(&mut self) {
        self.is_enabled = true;
    }

    /// Sets the indicators reporting state to disabled while maintaining the current
    /// indicator flags.
    pub fn disable(&mut self) {
        self.is_enabled = false;
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
    pub fn indicator_enabled(&self, status: &Indicator) -> bool {
        self.is_enabled
            && match status {
                Indicator::Service(_) => self.service,
                Indicator::Call(_) | Indicator::CallSetup(_) | Indicator::CallHeld(_) => true,
                Indicator::Signal(_) => self.signal,
                Indicator::Roam(_) => self.roam,
                Indicator::BatteryLevel(_) => self.batt_chg,
            }
    }
}

impl Default for IndicatorsReporting {
    /// The default indicators reporting state is disabled.
    /// Per HFP v1.8 Section 4.2.1.3, the HF will _always_ request to enable indicators
    /// reporting in the SLCI procedure (See +CMER AT command).
    fn default() -> Self {
        Self::new_disabled()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_indicators_reporting_is_disabled_with_all_indicators_enabled() {
        let default = IndicatorsReporting::default();
        assert!(!default.is_enabled);
        assert!(default.service);
        assert!(default.signal);
        assert!(default.roam);
        assert!(default.batt_chg);
    }

    #[test]
    fn indicator_flags_are_updated_from_bool_flags() {
        // No flags is OK, no updates.
        let mut status = IndicatorsReporting::default();
        let empty = vec![];
        let expected = status.clone();
        status.update_from_flags(empty);
        assert_eq!(status, expected);

        // An incomplete set of flags is OK (5 out of 7 supplied). The Call, Call Held, Call Setup
        // flags will not be overridden.
        let mut status = IndicatorsReporting::default();
        let incomplete = vec![Some(false), None, Some(false), Some(true)];
        let expected = IndicatorsReporting {
            is_enabled: false,
            service: false,
            signal: true,
            roam: true,
            batt_chg: true,
        };
        status.update_from_flags(incomplete);
        assert_eq!(status, expected);

        // A typical set of flags (all 7) is OK.
        let mut status = IndicatorsReporting::default();
        let flags =
            vec![None, Some(false), Some(false), Some(true), Some(false), Some(false), None];
        let expected = IndicatorsReporting {
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
        let mut status = IndicatorsReporting::default();
        let too_many =
            vec![None, None, None, None, Some(true), Some(false), None, Some(true), Some(false)];
        let expected = IndicatorsReporting {
            is_enabled: false,
            service: true,
            signal: true,
            roam: false,
            batt_chg: true,
        };
        status.update_from_flags(too_many);
        assert_eq!(status, expected);
    }

    #[test]
    fn toggling_indicators_reporting_maintains_same_indicators() {
        let mut status = IndicatorsReporting::default();
        assert!(!status.is_enabled);

        // Unset a specific indicator.
        status.set_batt_chg(false);

        // Toggling indicators reporting should preserve indicator values.
        let expected1 = IndicatorsReporting { is_enabled: false, ..status.clone() };
        status.disable();
        assert_eq!(status, expected1);
        status.disable();
        assert_eq!(status, expected1);

        status.enable();
        let expected2 = IndicatorsReporting { is_enabled: true, ..expected1.clone() };
        assert_eq!(status, expected2);
        assert!(status.is_enabled);
    }

    #[test]
    fn indicator_enabled_is_false_when_indicators_reporting_disabled() {
        let status = IndicatorsReporting::new_disabled();

        // Even though all the individual indicator values are toggled on, we expect
        // `indicator_enabled` to return false because indicators reporting is disabled.
        assert!(!status.indicator_enabled(&Indicator::Service(0)));
        assert!(!status.indicator_enabled(&Indicator::Call(0)));
    }

    #[test]
    fn indicator_enabled_check_returns_expected_result() {
        let mut status = IndicatorsReporting::new_enabled();
        status.batt_chg = false;
        status.roam = false;

        assert!(status.indicator_enabled(&Indicator::Service(0)));
        assert!(status.indicator_enabled(&Indicator::Signal(0)));
        assert!(status.indicator_enabled(&Indicator::Call(0)));
        assert!(status.indicator_enabled(&Indicator::CallSetup(0)));
        assert!(status.indicator_enabled(&Indicator::CallHeld(0)));
        assert!(!status.indicator_enabled(&Indicator::Roam(0)));
        assert!(!status.indicator_enabled(&Indicator::BatteryLevel(0)));
    }
}
