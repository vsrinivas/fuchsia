// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{
    indicators::AgIndicatorsReporting, service_level_connection::SlcState, update::AgUpdate,
};
use at_commands as at;

/// The Hf may request to enable or disable indicators via this procedure. Defined in
/// HFP v1.8 Section 4.35.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug, Default)]
pub struct IndicatorsActivationProcedure {
    /// The current state of the procedure
    terminated: bool,
}

impl IndicatorsActivationProcedure {
    pub fn new() -> Self {
        Self::default()
    }
}

impl Procedure for IndicatorsActivationProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::Indicators
    }

    fn hf_update(&mut self, update: at::Command, state: &mut SlcState) -> ProcedureRequest {
        match (self.terminated, update) {
            (false, at::Command::Bia { indrep }) => {
                self.terminated = true;
                state.ag_indicator_events_reporting.update_from_flags(indrep);
                AgUpdate::Ok.into()
            }
            (false, at::Command::Cmer { mode, ind, .. }) => {
                self.terminated = true;
                if mode == AgIndicatorsReporting::EVENT_REPORTING_MODE
                    && state.ag_indicator_events_reporting.set_reporting_status(ind).is_ok()
                {
                    AgUpdate::Ok.into()
                } else {
                    AgUpdate::Error.into()
                }
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = IndicatorsActivationProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::Indicators);
    }

    #[test]
    fn unexpected_hf_update_returns_error() {
        let mut procedure = IndicatorsActivationProcedure::new();
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
        let mut procedure = IndicatorsActivationProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_ag = AgUpdate::ThreeWaySupport;
        assert_matches!(
            procedure.ag_update(random_ag, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn update_with_valid_indicators_produces_ok_request() {
        let mut procedure = IndicatorsActivationProcedure::new();
        let mut state = SlcState::default();
        assert!(!procedure.is_terminated());

        let indicators = vec![Some(false), Some(true), Some(false), None, Some(true), None, None];
        let update = at::Command::Bia { indrep: indicators };
        let expected_messages = vec![at::Response::Ok];
        assert_matches!(procedure.hf_update(update.clone(), &mut state), ProcedureRequest::SendMessages(m) if m == expected_messages);
        assert!(procedure.is_terminated());

        // The SlcState's indicators should be updated with the new indicator values.
        // Note: The request to change Call & Call Setup were ignored since those must be enabled
        // at all times per HFP v1.8 Section 4.35.
        let mut expected_indicators = AgIndicatorsReporting::default();
        expected_indicators.set_service(false);
        expected_indicators.set_signal(true);
        assert_eq!(state.ag_indicator_events_reporting, expected_indicators);

        // Trying to send a request after procedure is terminated will fail.
        assert_matches!(
            procedure.hf_update(update, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(_))
        );
    }
}
