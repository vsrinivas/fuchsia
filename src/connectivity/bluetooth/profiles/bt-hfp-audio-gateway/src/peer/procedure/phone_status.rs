// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use at_commands as at;
use tracing::debug;

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};
use crate::peer::{service_level_connection::SlcState, update::AgUpdate};

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
            AgUpdate::PhoneStatusIndicator(status) if !self.responded => {
                self.responded = true;
                // Only send the update if indicator event reporting is enabled for the SLC and
                // the specific indicator is activated.
                if state.ag_indicator_events_reporting.indicator_enabled(&status) {
                    AgUpdate::PhoneStatusIndicator(status).into()
                } else {
                    debug!("PhoneStatus indicator {:?} disabled. Will not send to peer", status);
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
    use crate::peer::indicators::{
        AgIndicator, AgIndicatorsReporting, CALL_HELD_INDICATOR_INDEX, SIGNAL_INDICATOR_INDEX,
    };
    use assert_matches::assert_matches;

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
        let mut state = SlcState {
            ag_indicator_events_reporting: AgIndicatorsReporting::new_enabled(),
            ..SlcState::default()
        };

        assert!(!procedure.is_terminated());

        let status = AgIndicator::Signal(4);
        let update = AgUpdate::PhoneStatusIndicator(status);
        let expected_messages = vec![at::Response::Success(at::Success::Ciev {
            ind: SIGNAL_INDICATOR_INDEX as i64,
            value: 4,
        })];
        assert_matches!(procedure.ag_update(update, &mut state), ProcedureRequest::SendMessages(m) if m == expected_messages);
        assert!(procedure.is_terminated());
    }

    #[test]
    fn update_with_ind_reporting_disabled_produces_no_request() {
        let mut procedure = PhoneStatusProcedure::new();
        let mut state = SlcState {
            ag_indicator_events_reporting: AgIndicatorsReporting::new_disabled(),
            ..SlcState::default()
        };
        assert!(!procedure.is_terminated());

        let status = AgUpdate::PhoneStatusIndicator(AgIndicator::Signal(4));
        // Because indicator events reporting is disabled, we expect the procedure to not
        // request to send anything.
        assert_matches!(procedure.ag_update(status, &mut state), ProcedureRequest::None);
        assert!(procedure.is_terminated());
    }

    #[test]
    fn update_with_specific_indicator_disabled_produces_no_request() {
        // Explicitly disable the Battery Level indicator.
        let mut ag_indicator_events_reporting = AgIndicatorsReporting::new_enabled();
        ag_indicator_events_reporting.set_batt_chg(false);
        let mut state = SlcState { ag_indicator_events_reporting, ..SlcState::default() };

        let mut procedure = PhoneStatusProcedure::new();
        let status = AgUpdate::PhoneStatusIndicator(AgIndicator::BatteryLevel(1));
        // Because specifically the battery level indicator is disabled, we expect the procedure
        // to not request to send anything.
        assert_matches!(procedure.ag_update(status, &mut state), ProcedureRequest::None);
        assert!(procedure.is_terminated());

        // However, sending a different indicator is OK.
        let mut procedure = PhoneStatusProcedure::new();
        let status = AgUpdate::PhoneStatusIndicator(AgIndicator::CallHeld(1));
        let expected_messages = vec![at::Response::Success(at::Success::Ciev {
            ind: CALL_HELD_INDICATOR_INDEX as i64,
            value: 1,
        })];
        assert_matches!(procedure.ag_update(status, &mut state), ProcedureRequest::SendMessages(m) if m == expected_messages);
        assert!(procedure.is_terminated());
    }
}
