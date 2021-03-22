// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use at_commands as at;

use super::{AgUpdate, Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};
use crate::peer::service_level_connection::SlcState;

/// The supported phone status update indicators.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PhoneStatus {
    Service(u8),
    Call(u8),
    CallSetup(u8),
    CallHeld(u8),
    Signal(u8),
    Roam(u8),
    BatteryLevel(u8),
}

impl From<PhoneStatus> for at::Response {
    fn from(src: PhoneStatus) -> at::Response {
        match src {
            PhoneStatus::Service(v) => {
                at::Response::Success(at::Success::Ciev { ind: 1, value: v as i64 })
            }
            PhoneStatus::Call(v) => {
                at::Response::Success(at::Success::Ciev { ind: 2, value: v as i64 })
            }
            PhoneStatus::CallSetup(v) => {
                at::Response::Success(at::Success::Ciev { ind: 3, value: v as i64 })
            }
            PhoneStatus::CallHeld(v) => {
                at::Response::Success(at::Success::Ciev { ind: 4, value: v as i64 })
            }
            PhoneStatus::Signal(v) => {
                at::Response::Success(at::Success::Ciev { ind: 5, value: v as i64 })
            }
            PhoneStatus::Roam(v) => {
                at::Response::Success(at::Success::Ciev { ind: 6, value: v as i64 })
            }
            PhoneStatus::BatteryLevel(v) => {
                at::Response::Success(at::Success::Ciev { ind: 7, value: v as i64 })
            }
        }
    }
}

impl From<PhoneStatus> for AgUpdate {
    fn from(src: PhoneStatus) -> Self {
        Self::PhoneStatusIndicator(src)
    }
}

/// Represents the Transfer of Phone Status Indication procedures as defined in
/// HFP v1.8 Section 4.4 - 4.7.
/// Note: All four procedures use the same underlying AT command (CIEV) with differing
/// status codes. The structure and state machines are the same, and therefore this procedure
/// will handle any of them.
///
/// The AG may send an unsolicited phone status update to the HF via this procedure.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
pub struct PhoneStatusProcedure {
    // Whether the procedure has sent the phone status to the HF.
    responded: bool,
}

impl PhoneStatusProcedure {
    pub fn new() -> Self {
        Self { responded: false }
    }
}

impl Procedure for PhoneStatusProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::PhoneStatus
    }

    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        ProcedureRequest::Error(ProcedureError::UnexpectedHf(update))
    }

    fn ag_update(&mut self, update: AgUpdate, state: &mut SlcState) -> ProcedureRequest {
        match update {
            update @ AgUpdate::PhoneStatusIndicator(..) if !self.responded => {
                self.responded = true;
                // Only send the update if indicator event reporting is enabled for the SLC.
                if state.indicator_events_reporting {
                    update.into()
                } else {
                    ProcedureRequest::None
                }
            }
            _ => ProcedureRequest::Error(ProcedureError::UnexpectedAg(update)),
        }
    }

    /// Returns true if the Procedure is finished.
    fn is_terminated(&self) -> bool {
        self.responded
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = PhoneStatusProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::PhoneStatus);
    }

    #[test]
    fn unexpected_hf_update_returns_error() {
        let mut procedure = PhoneStatusProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_hf = at::Command::CindRead {};
        assert_matches!(
            procedure.hf_update(random_hf, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(_))
        );
    }

    #[test]
    fn unexpected_ag_update_returns_error() {
        let mut procedure = PhoneStatusProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_ag = AgUpdate::ThreeWaySupport;
        assert_matches!(
            procedure.ag_update(random_ag, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn update_with_ind_reporting_enabled_produces_expected_request() {
        let mut procedure = PhoneStatusProcedure::new();
        let mut state = SlcState { indicator_events_reporting: true, ..SlcState::default() };

        assert!(!procedure.is_terminated());

        let status = PhoneStatus::Signal(4);
        let update = AgUpdate::PhoneStatusIndicator(status);
        let expected_messages = vec![at::Response::Success(at::Success::Ciev { ind: 5, value: 4 })];
        assert_matches!(procedure.ag_update(update, &mut state), ProcedureRequest::SendMessages(m) if m == expected_messages);
        assert!(procedure.is_terminated());
    }

    #[test]
    fn update_with_ind_reporting_disabled_produces_expected_request() {
        let mut procedure = PhoneStatusProcedure::new();
        let mut state = SlcState { indicator_events_reporting: false, ..SlcState::default() };
        assert!(!procedure.is_terminated());

        let status = AgUpdate::PhoneStatusIndicator(PhoneStatus::Signal(4));
        // Because indicator events reporting is disabled, we expect the procedure to not
        // request to send anything.
        assert_matches!(procedure.ag_update(status, &mut state), ProcedureRequest::None);
        assert!(procedure.is_terminated());
    }
}
