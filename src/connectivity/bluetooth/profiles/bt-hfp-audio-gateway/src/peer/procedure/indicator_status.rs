// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{service_level_connection::SlcState, update::AgUpdate};
use at_commands as at;

/// The HF requests supported AG indicators via this procedure. See HFP v1.8, Section 4.2.1.3.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug, Default)]
pub struct IndicatorStatusProcedure {
    /// The current state of the procedure
    terminated: bool,
}

impl IndicatorStatusProcedure {
    /// Create a new IndicatorStatus procedure in the Start state.
    pub fn new() -> Self {
        Self::default()
    }
}

impl Procedure for IndicatorStatusProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::IndicatorStatus
    }

    fn hf_update(&mut self, update: at::Command, state: &mut SlcState) -> ProcedureRequest {
        match (self.terminated, update) {
            (false, at::Command::CindRead {}) => {
                self.terminated = true;
                AgUpdate::IndicatorStatus(state.ag_indicator_status).into()
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
        let marker = IndicatorStatusProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::IndicatorStatus);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = IndicatorStatusProcedure::new();
        let req = proc.hf_update(at::Command::CopsRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_with_ok_response() {
        let mut proc = IndicatorStatusProcedure::new();
        let req = proc.hf_update(at::Command::CindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::SendMessages(_));
        assert!(proc.is_terminated());
    }
}
