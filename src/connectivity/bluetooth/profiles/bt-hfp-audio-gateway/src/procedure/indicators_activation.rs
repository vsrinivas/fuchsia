// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{AgUpdate, Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::{
    peer::service_level_connection::SlcState, protocol::indicators::AgIndicatorsReporting,
};
use at_commands as at;

/// Converts the indicator activeness flags (represented as Strings) to a vector of
/// optional flags. Returns the set of flags or an Error if the `indicators` are formatted
/// incorrectly.
// TODO(fxbug.dev/73374): Remove this once the AT library represents the command as a Vec<Option<bool>>.
fn to_flags(indicators: Vec<String>) -> Result<Vec<Option<bool>>, ()> {
    let result: Vec<Result<Option<bool>, ()>> = indicators
        .into_iter()
        .map(|ind| match ind.as_str() {
            "" => Ok(None),
            "1" => Ok(Some(true)),
            "0" => Ok(Some(false)),
            _ => Err(()),
        })
        .collect();

    result.into_iter().collect()
}

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
                if let Ok(flags) = to_flags(indrep) {
                    state.ag_indicator_events_reporting.update_from_flags(flags);
                    AgUpdate::Ok.into()
                } else {
                    // Per HFP v1.8 Section 4.35, if the command is incorrectly formatted,
                    // send an ERROR result code.
                    AgUpdate::Error.into()
                }
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
    use matches::assert_matches;

    #[test]
    fn to_flags_conversion_produces_expected_results() {
        let empty = Vec::new();
        let expected: Vec<Option<bool>> = vec![];
        assert_matches!(to_flags(empty), Ok(v) if v == expected);

        let valid = vec!["1".to_string(), String::new(), "0".to_string()];
        let expected = vec![Some(true), None, Some(false)];
        assert_matches!(to_flags(valid), Ok(v) if v == expected);

        let invalid = vec!["0".to_string(), "foo".to_string()];
        assert_matches!(to_flags(invalid), Err(()));
    }

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
    fn update_with_invalid_indicators_produces_error_request() {
        let mut procedure = IndicatorsActivationProcedure::new();
        let mut state = SlcState::default();
        assert!(!procedure.is_terminated());

        // "8" is not a supported value for an indicator.
        let indicators = vec![
            "0".to_string(),
            "1".to_string(),
            "0".to_string(),
            String::new(),
            "8".to_string(),
            "1".to_string(),
        ];
        let update = at::Command::Bia { indrep: indicators };
        let expected_messages = vec![at::Response::Error];
        assert_matches!(procedure.hf_update(update, &mut state), ProcedureRequest::SendMessages(m) if m == expected_messages);
        assert!(procedure.is_terminated());
    }

    #[test]
    fn update_with_valid_indicators_produces_ok_request() {
        let mut procedure = IndicatorsActivationProcedure::new();
        let mut state = SlcState::default();
        assert!(!procedure.is_terminated());

        let indicators = vec![
            "0".to_string(),
            "1".to_string(),
            "0".to_string(),
            String::new(),
            "1".to_string(),
            String::new(),
            String::new(),
        ];
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
