// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {at_commands as at, fidl_fuchsia_bluetooth_hfp::CallState};

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
    pub call: Call,
    pub callsetup: CallSetup,
    pub callheld: CallHeld,
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

impl From<Call> for Indicator {
    fn from(src: Call) -> Self {
        Indicator::Call(src as u8)
    }
}

impl From<CallSetup> for Indicator {
    fn from(src: CallSetup) -> Self {
        Indicator::CallSetup(src as u8)
    }
}

impl From<CallHeld> for Indicator {
    fn from(src: CallHeld) -> Self {
        Indicator::CallHeld(src as u8)
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

/// The Call Indicator as specified in HFP v1.8, Section 4.10.1
#[derive(PartialEq, Clone, Copy, Debug)]
pub enum Call {
    /// There are no calls present in the AG (active or held).
    None,
    /// There is at least one call present in the AG (active or held).
    Some,
}

impl Default for Call {
    fn default() -> Self {
        Self::None
    }
}

impl From<Call> for bool {
    fn from(call: Call) -> Self {
        match call {
            Call::None => false,
            Call::Some => true,
        }
    }
}

impl From<bool> for Call {
    fn from(call: bool) -> Self {
        match call {
            false => Self::None,
            true => Self::Some,
        }
    }
}

impl Call {
    /// Find the Call state based on all the calls in `iter`.
    pub fn find(mut iter: impl Iterator<Item = CallState>) -> Self {
        iter.any(|state| [CallState::OngoingActive, CallState::OngoingHeld].contains(&state)).into()
    }
}

/// The Callsetup Indicator as specified in HFP v1.8, Section 4.10.2
#[derive(PartialEq, Clone, Copy, Debug)]
pub enum CallSetup {
    /// No call setup in progress.
    None = 0,
    /// Incoming call setup in progress.
    Incoming = 1,
    /// Outgoing call setup in dialing state.
    OutgoingDialing = 2,
    /// Outgoing call setup in alerting state.
    OutgoingAlerting = 3,
}

impl Default for CallSetup {
    fn default() -> Self {
        Self::None
    }
}

impl CallSetup {
    /// Find CallSetup state based on the first call in `iter` that is in a callsetup state.
    pub fn find(mut iter: impl Iterator<Item = CallState>) -> Self {
        iter.find(|state| {
            [
                CallState::IncomingRinging,
                CallState::IncomingWaiting,
                CallState::OutgoingAlerting,
                CallState::OutgoingDialing,
            ]
            .contains(&state)
        })
        .map(CallSetup::from)
        .unwrap_or(CallSetup::None)
    }
}

impl From<CallState> for CallSetup {
    fn from(state: CallState) -> Self {
        match state {
            CallState::IncomingRinging | CallState::IncomingWaiting => Self::Incoming,
            CallState::OutgoingDialing => Self::OutgoingDialing,
            CallState::OutgoingAlerting => Self::OutgoingAlerting,
            _ => Self::None,
        }
    }
}

/// The Callheld Indicator as specified in HFP v1.8, Section 4.10.3
#[derive(PartialEq, Clone, Copy, Debug)]
pub enum CallHeld {
    /// No calls held.
    None = 0,
    /// Call is placed on hold or active/held calls swapped (The AG has both an active AND a held
    /// call).
    HeldAndActive = 1,
    /// Call on hold, no active call.
    Held = 2,
}

impl Default for CallHeld {
    fn default() -> Self {
        Self::None
    }
}

impl CallHeld {
    /// Find the CallHeld state based on all calls in `iter`.
    pub fn find(mut iter: impl Iterator<Item = CallState> + Clone) -> Self {
        let any_held = iter.clone().any(|state| state == CallState::OngoingHeld);
        let any_active = iter.any(|state| state == CallState::OngoingActive);
        match (any_held, any_active) {
            (true, false) => CallHeld::Held,
            (true, true) => CallHeld::HeldAndActive,
            (false, _) => CallHeld::None,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct CallIndicators {
    pub call: Call,
    pub callsetup: CallSetup,
    pub callheld: CallHeld,
}

impl CallIndicators {
    /// Find CallIndicators based on all items in `iter`.
    pub fn find(iter: impl Iterator<Item = CallState> + Clone) -> Self {
        let call = Call::find(iter.clone());
        let callsetup = CallSetup::find(iter.clone());
        let callheld = CallHeld::find(iter);
        CallIndicators { call, callsetup, callheld }
    }

    /// A list of all the statuses that have changed between `other` and self.
    /// The values in the list are the values found in `self`.
    pub fn difference(&self, other: Self) -> CallIndicatorsUpdates {
        let mut changes = CallIndicatorsUpdates::default();
        if other.call != self.call {
            changes.call = Some(self.call);
        }
        if other.callsetup != self.callsetup {
            changes.callsetup = Some(self.callsetup);
        }
        if other.callheld != self.callheld {
            changes.callheld = Some(self.callheld);
        }
        changes
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct CallIndicatorsUpdates {
    pub call: Option<Call>,
    pub callsetup: Option<CallSetup>,
    pub callheld: Option<CallHeld>,
}

impl CallIndicatorsUpdates {
    /// Returns true if all fields are `None`
    pub fn is_empty(&self) -> bool {
        self.call.is_none() && self.callsetup.is_none() && self.callheld.is_none()
    }

    /// Returns a Vec of all updated indicators. This vec is ordered by Indicator index.
    pub fn to_vec(&self) -> Vec<Indicator> {
        let mut v = vec![];
        v.extend(self.call.map(|i| Indicator::Call(i as u8)));
        v.extend(self.callsetup.map(|i| Indicator::CallSetup(i as u8)));
        v.extend(self.callheld.map(|i| Indicator::CallHeld(i as u8)));
        v
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

    #[test]
    fn find_call() {
        let states = vec![];
        let call = Call::find(states.into_iter());
        assert_eq!(call, Call::None);

        let states = vec![CallState::Terminated];
        let call = Call::find(states.into_iter());
        assert_eq!(call, Call::None);

        let states = vec![CallState::OngoingActive];
        let call = Call::find(states.into_iter());
        assert_eq!(call, Call::Some);

        let states = vec![CallState::OngoingHeld];
        let call = Call::find(states.into_iter());
        assert_eq!(call, Call::Some);

        let states = vec![CallState::OngoingHeld, CallState::Terminated];
        let call = Call::find(states.into_iter());
        assert_eq!(call, Call::Some);

        let states = vec![CallState::OngoingHeld, CallState::OngoingActive];
        let call = Call::find(states.into_iter());
        assert_eq!(call, Call::Some);
    }

    #[test]
    fn find_callsetup() {
        let states = vec![];
        let setup = CallSetup::find(states.into_iter());
        assert_eq!(setup, CallSetup::None);

        let states = vec![CallState::Terminated];
        let setup = CallSetup::find(states.into_iter());
        assert_eq!(setup, CallSetup::None);

        let states = vec![CallState::IncomingRinging];
        let setup = CallSetup::find(states.into_iter());
        assert_eq!(setup, CallSetup::Incoming);

        let states = vec![CallState::IncomingWaiting];
        let setup = CallSetup::find(states.into_iter());
        assert_eq!(setup, CallSetup::Incoming);

        let states = vec![CallState::OutgoingAlerting];
        let setup = CallSetup::find(states.into_iter());
        assert_eq!(setup, CallSetup::OutgoingAlerting);

        let states = vec![CallState::OutgoingDialing];
        let setup = CallSetup::find(states.into_iter());
        assert_eq!(setup, CallSetup::OutgoingDialing);

        // The first setup state is used.
        let states = vec![CallState::OutgoingDialing, CallState::IncomingRinging];
        let setup = CallSetup::find(states.into_iter());
        assert_eq!(setup, CallSetup::OutgoingDialing);

        // Other states have no effect
        let states = vec![CallState::Terminated, CallState::IncomingRinging];
        let setup = CallSetup::find(states.into_iter());
        assert_eq!(setup, CallSetup::Incoming);
    }

    #[test]
    fn find_call_held() {
        let states = vec![];
        let held = CallHeld::find(states.into_iter());
        assert_eq!(held, CallHeld::None);

        let states = vec![CallState::OngoingHeld];
        let held = CallHeld::find(states.into_iter());
        assert_eq!(held, CallHeld::Held);

        // Active without Held is None.
        let states = vec![CallState::OngoingActive];
        let held = CallHeld::find(states.into_iter());
        assert_eq!(held, CallHeld::None);

        // Other states have no effect.
        let states = vec![CallState::OngoingHeld, CallState::Terminated];
        let held = CallHeld::find(states.into_iter());
        assert_eq!(held, CallHeld::Held);

        // Held then active produces expected result.
        let states = vec![CallState::OngoingHeld, CallState::OngoingActive];
        let held = CallHeld::find(states.into_iter());
        assert_eq!(held, CallHeld::HeldAndActive);

        // And so does the reverse.
        let states = vec![CallState::OngoingActive, CallState::OngoingHeld];
        let held = CallHeld::find(states.into_iter());
        assert_eq!(held, CallHeld::HeldAndActive);
    }

    #[test]
    fn find_call_indicators() {
        let states = vec![];
        let ind = CallIndicators::find(states.into_iter());
        assert_eq!(ind, CallIndicators::default());

        let states = vec![CallState::OngoingHeld, CallState::IncomingRinging];
        let ind = CallIndicators::find(states.into_iter());
        let expected = CallIndicators {
            call: Call::Some,
            callsetup: CallSetup::Incoming,
            callheld: CallHeld::Held,
        };
        assert_eq!(ind, expected);
    }

    #[test]
    fn call_indicators_differences() {
        let a = CallIndicators::default();
        let b = CallIndicators { ..a };
        assert!(b.difference(a).is_empty());

        let a = CallIndicators::default();
        let b = CallIndicators { call: Call::Some, ..a };
        let expected =
            CallIndicatorsUpdates { call: Some(Call::Some), ..CallIndicatorsUpdates::default() };
        assert_eq!(b.difference(a), expected);

        let a = CallIndicators::default();
        let b = CallIndicators { call: Call::Some, callheld: CallHeld::Held, ..a };
        let expected = CallIndicatorsUpdates {
            call: Some(Call::Some),
            callheld: Some(CallHeld::Held),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(b.difference(a), expected);

        let a = CallIndicators { call: Call::Some, ..CallIndicators::default() };
        let b = CallIndicators { callsetup: CallSetup::Incoming, ..a };
        let expected = CallIndicatorsUpdates {
            callsetup: Some(CallSetup::Incoming),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(b.difference(a), expected);
    }

    #[test]
    fn call_indicator_updates_is_empty() {
        let mut updates = CallIndicatorsUpdates::default();
        assert!(updates.is_empty());

        updates.call = Some(Call::Some);
        assert!(!updates.is_empty());

        let mut updates = CallIndicatorsUpdates::default();
        updates.callsetup = Some(CallSetup::Incoming);
        assert!(!updates.is_empty());

        let mut updates = CallIndicatorsUpdates::default();
        updates.callheld = Some(CallHeld::Held);
        assert!(!updates.is_empty());
    }

    #[test]
    fn call_indicator_updates_to_vec() {
        let mut updates = CallIndicatorsUpdates::default();
        assert_eq!(updates.to_vec(), vec![]);

        let call = Call::Some;
        updates.call = Some(call);
        assert_eq!(updates.to_vec(), vec![Indicator::Call(call as u8)]);

        let callsetup = CallSetup::Incoming;
        updates.callsetup = Some(callsetup);
        let expected = vec![Indicator::Call(call as u8), Indicator::CallSetup(callsetup as u8)];
        assert_eq!(updates.to_vec(), expected);

        let callheld = CallHeld::Held;
        updates.callheld = Some(callheld);
        let expected = vec![
            Indicator::Call(call as u8),
            Indicator::CallSetup(callsetup as u8),
            Indicator::CallHeld(callheld as u8),
        ];
        assert_eq!(updates.to_vec(), expected);
    }
}
