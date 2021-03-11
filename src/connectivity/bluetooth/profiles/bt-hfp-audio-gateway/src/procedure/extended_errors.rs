// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::at::{AtAgMessage, AtHfMessage};
use crate::peer::service_level_connection::SlcState;

/// Represents the current state of the Hf request to report extended Audio Gateway error result
/// codes as defined in HFP v1.8, Section 4.9.
#[derive(Debug, PartialEq, Clone, Copy)]
enum State {
    /// Initial state of the Procedure.
    Start,
    /// Terminal state of the procedure.
    Terminated,
}

impl State {
    /// Transition to the next state in the ExtendedErrors procedure.
    fn transition(&mut self) {
        match *self {
            Self::Start => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

/// The Hf may request that the Audio Gateway report extended error result codes as defined in
/// HFP v1.8, Section 4.9.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct ExtendedErrorsProcedure {
    /// The current state of the procedure
    state: State,
}

impl Default for ExtendedErrorsProcedure {
    fn default() -> Self {
        Self { state: State::Start }
    }
}

impl ExtendedErrorsProcedure {
    /// Create a new ExtendedErrors procedure in the Start state.
    pub fn new() -> Self {
        Self { state: State::Start }
    }
}

impl Procedure for ExtendedErrorsProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::ExtendedErrors
    }

    fn hf_update(&mut self, update: AtHfMessage, state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::Start, AtHfMessage::Cmee(enable)) => {
                self.state.transition();
                state.extended_errors = enable;
                ProcedureRequest::SendMessage(AtAgMessage::Ok)
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AtAgMessage, _state: &mut SlcState) -> ProcedureRequest {
        ProcedureRequest::Error(ProcedureError::UnexpectedAg(update))
    }

    fn is_terminated(&self) -> bool {
        self.state == State::Terminated
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = ExtendedErrorsProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::ExtendedErrors);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = ExtendedErrorsProcedure::new();
        let req = proc.hf_update(AtHfMessage::AgIndStat, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AtAgMessage::AgThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_with_ok_response() {
        let mut proc = ExtendedErrorsProcedure::new();
        let req = proc.hf_update(AtHfMessage::Cmee(true), &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::SendMessage(_));
        assert!(proc.is_terminated());
    }

    #[test]
    fn procedure_with_errors_disabled_ok_response() {
        let mut proc = ExtendedErrorsProcedure::new();
        let req = proc.hf_update(AtHfMessage::Cmee(false), &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::SendMessage(_));
        assert!(proc.is_terminated());
    }
}
