// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::peer::indicators::AgIndicator,
    fidl_fuchsia_bluetooth_hfp::{CallDirection as FidlCallDirection, CallState},
};

/// The fuchsia.bluetooth.hfp library representation of a Number.
pub type FidlNumber = String;

/// The direction of call initiation.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum Direction {
    /// Call originated on this device. This is also known as an Outgoing call.
    MobileOriginated,
    /// Call is terminated on this device. This is also known as an Incoming call.
    MobileTerminated,
}

impl From<FidlCallDirection> for Direction {
    fn from(x: FidlCallDirection) -> Self {
        match x {
            FidlCallDirection::MobileOriginated => Self::MobileOriginated,
            FidlCallDirection::MobileTerminated => Self::MobileTerminated,
        }
    }
}

impl From<Direction> for i64 {
    fn from(x: Direction) -> Self {
        match x {
            Direction::MobileOriginated => 0,
            Direction::MobileTerminated => 1,
        }
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
        iter.any(|state| {
            [CallState::OngoingActive, CallState::OngoingHeld, CallState::TransferredToAg]
                .contains(&state)
        })
        .into()
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
    /// There is at least one call in the IncomingWaiting state. `callwaiting` is distinct from
    /// other fields in that it doesn't map to a specific CIEV phone status indicator.
    pub callwaiting: bool,
}

impl CallIndicators {
    /// Find CallIndicators based on all items in `iter`.
    pub fn find(mut iter: impl Iterator<Item = CallState> + Clone) -> Self {
        let call = Call::find(iter.clone());
        let callsetup = CallSetup::find(iter.clone());
        let callheld = CallHeld::find(iter.clone());
        let callwaiting = iter.any(|c| c == CallState::IncomingWaiting);
        CallIndicators { call, callsetup, callheld, callwaiting }
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
        if self.callwaiting && !other.callwaiting {
            changes.callwaiting = true;
        }
        changes
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct CallIndicatorsUpdates {
    pub call: Option<Call>,
    pub callsetup: Option<CallSetup>,
    pub callheld: Option<CallHeld>,
    /// Indicates whether there is a call that has changed to the CallWaiting state in this update.
    pub callwaiting: bool,
}

impl CallIndicatorsUpdates {
    /// Returns true if all fields are `None` or false.
    pub fn is_empty(&self) -> bool {
        self.call.is_none()
            && self.callsetup.is_none()
            && self.callheld.is_none()
            && !self.callwaiting
    }

    /// Returns a Vec of all updated AG indicators. This vec is ordered by Indicator index.
    pub fn to_vec(&self) -> Vec<AgIndicator> {
        let mut v = vec![];
        v.extend(self.call.map(|i| AgIndicator::Call(i as u8)));
        v.extend(self.callsetup.map(|i| AgIndicator::CallSetup(i as u8)));
        v.extend(self.callheld.map(|i| AgIndicator::CallHeld(i as u8)));
        v
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
    fn find_call_indicators() {
        let states = vec![];
        let ind = CallIndicators::find(states.into_iter());
        assert_eq!(ind, CallIndicators::default());

        let states = vec![CallState::OngoingHeld, CallState::IncomingRinging];
        let ind = CallIndicators::find(states.into_iter());
        let expected = CallIndicators {
            call: Call::Some,
            callsetup: CallSetup::Incoming,
            callwaiting: false,
            callheld: CallHeld::Held,
        };
        assert_eq!(ind, expected);
    }

    #[fuchsia::test]
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

        let a = CallIndicators::default();
        let b = CallIndicators { callsetup: CallSetup::Incoming, callwaiting: true, ..a };
        let expected = CallIndicatorsUpdates {
            callsetup: Some(CallSetup::Incoming),
            callwaiting: true,
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(b.difference(a), expected);

        // reverse: going from b to a.
        let expected = CallIndicatorsUpdates {
            callsetup: Some(CallSetup::None),
            callwaiting: false,
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(a.difference(b), expected);
    }

    #[fuchsia::test]
    fn call_indicator_updates_is_empty() {
        let mut updates = CallIndicatorsUpdates::default();
        assert!(updates.is_empty());

        updates.call = Some(Call::Some);
        assert!(!updates.is_empty());

        let mut updates = CallIndicatorsUpdates::default();
        updates.callsetup = Some(CallSetup::Incoming);
        assert!(!updates.is_empty());

        let mut updates = CallIndicatorsUpdates::default();
        updates.callwaiting = true;
        assert!(!updates.is_empty());

        let mut updates = CallIndicatorsUpdates::default();
        updates.callheld = Some(CallHeld::Held);
        assert!(!updates.is_empty());
    }

    #[fuchsia::test]
    fn call_indicator_updates_to_vec() {
        let mut updates = CallIndicatorsUpdates::default();
        assert_eq!(updates.to_vec(), vec![]);

        let call = Call::Some;
        updates.call = Some(call);
        assert_eq!(updates.to_vec(), vec![AgIndicator::Call(call as u8)]);

        let callsetup = CallSetup::Incoming;
        updates.callsetup = Some(callsetup);
        let expected = vec![AgIndicator::Call(call as u8), AgIndicator::CallSetup(callsetup as u8)];
        assert_eq!(updates.to_vec(), expected);

        let callheld = CallHeld::Held;
        updates.callheld = Some(callheld);
        let expected = vec![
            AgIndicator::Call(call as u8),
            AgIndicator::CallSetup(callsetup as u8),
            AgIndicator::CallHeld(callheld as u8),
        ];
        assert_eq!(updates.to_vec(), expected);
    }
}
