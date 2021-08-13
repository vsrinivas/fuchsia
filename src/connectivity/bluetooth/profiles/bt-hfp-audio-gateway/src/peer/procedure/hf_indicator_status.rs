// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{service_level_connection::SlcState, update::AgUpdate};
use at_commands as at;

/// The HF tests AG support for HF indicators and reads indicator enabled statuses via this
/// Procedure. See HFP v1.8 Section 4.2.1.4.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct SupportedHfIndicatorsProcedure {
    terminated: bool,
}

impl Default for SupportedHfIndicatorsProcedure {
    fn default() -> Self {
        Self { terminated: false }
    }
}

impl SupportedHfIndicatorsProcedure {
    /// Create a new Transfer HF Indicator procedure in the Start state.
    pub fn new() -> Self {
        Self::default()
    }
}

impl Procedure for SupportedHfIndicatorsProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::SupportedHfIndicators
    }

    fn hf_update(&mut self, update: at::Command, state: &mut SlcState) -> ProcedureRequest {
        match (self.terminated, update) {
            (false, at::Command::BindTest {}) => {
                self.terminated = true;
                AgUpdate::SupportedHfIndicators { safety: true, battery: true }.into()
            }
            (false, at::Command::BindRead {}) => {
                self.terminated = true;
                AgUpdate::SupportedHfIndicatorStatus(state.hf_indicators).into()
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
    fn correct_marker() {
        let marker = SupportedHfIndicatorsProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::SupportedHfIndicators);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = SupportedHfIndicatorsProcedure::new();
        let req = proc.hf_update(at::Command::CindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_test_command_sends_messages() {
        let mut proc = SupportedHfIndicatorsProcedure::new();
        assert!(!proc.is_terminated());
        let req = proc.hf_update(at::Command::BindTest {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::SendMessages(_));
        assert!(proc.is_terminated());
    }

    #[test]
    fn procedure_read_command_sends_messages() {
        let mut proc = SupportedHfIndicatorsProcedure::new();
        assert!(!proc.is_terminated());
        let req = proc.hf_update(at::Command::BindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::SendMessages(_));
        assert!(proc.is_terminated());
    }
}
