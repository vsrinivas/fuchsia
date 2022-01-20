// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{
    calls::Call, service_level_connection::SlcState, slc_request::SlcRequest, update::AgUpdate,
};

use {at_commands as at, fidl_fuchsia_bluetooth_hfp::CallState};

/// Represents the current state of the Hf request to query the list of current calls as defined
/// in HFP v1.8, Section 4.32.1.
#[derive(Debug, PartialEq, Clone, Copy)]
enum State {
    /// Initial state of the Procedure.
    Start,
    /// A request has been received from the HF to query the list of current calls.
    SetRequest,
    /// Terminal state of the procedure.
    Terminated,
}

impl State {
    /// Transition to the next state in the QueryCurrentCalls procedure.
    fn transition(&mut self) {
        match *self {
            Self::Start => *self = Self::SetRequest,
            Self::SetRequest => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

/// The HF query the list of current calls via this procedure. See HFP v1.8, Section 4.32.1
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct QueryCurrentCallsProcedure {
    /// The current state of the procedure
    state: State,
}

impl Default for QueryCurrentCallsProcedure {
    fn default() -> Self {
        Self { state: State::Start }
    }
}

impl QueryCurrentCallsProcedure {
    /// Create a new QueryCurrentCalls procedure in the Start state.
    pub fn new() -> Self {
        Self { state: State::Start }
    }
}

impl Procedure for QueryCurrentCallsProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::QueryCurrentCalls
    }

    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::Start, at::Command::Clcc { .. }) => {
                self.state.transition();
                let response = Box::new(AgUpdate::CurrentCalls);
                SlcRequest::QueryCurrentCalls { response }.into()
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::SetRequest, update @ AgUpdate::CurrentCalls(..)) => {
                self.state.transition();
                update.into()
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedAg(update)),
        }
    }

    fn is_terminated(&self) -> bool {
        self.state == State::Terminated
    }
}

/// Convert a CallState value to a status integer as defined in HFP v1.8, Section 4.34.2.
/// Returns `None` if state does not have a valid status representation.
fn call_state_to_status(state: CallState) -> Option<i64> {
    use CallState::*;
    let status = match state {
        OngoingActive => 0,
        OngoingHeld => 1,
        OutgoingDialing => 2,
        OutgoingAlerting => 3,
        IncomingRinging => 4,
        IncomingWaiting => 5,
        _ => return None,
    };
    Some(status)
}

/// Build an AT CLCC response from a number. See HFP v1.8, Section 4.34.2 for information on the
/// +CLCC fields.
///
/// Returns None if the call is not ongoing (i.e. Terminated or TransferredToAg).
/// The CLCC response can not represent calls that are not ongoing.
pub fn build_clcc_response(call: Call) -> Option<at::Response> {
    let status = if let Some(status) = call_state_to_status(call.state) {
        status
    } else {
        return None;
    };

    Some(at::success(at::Success::Clcc {
        index: call.index as i64,
        dir: call.direction.into(),
        status,
        mode: 0,
        mpty: 0,
        ty: call.number.type_(),
        number: call.number.into(),
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::peer::calls::{Direction, Number};
    use assert_matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = QueryCurrentCallsProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::QueryCurrentCalls);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = QueryCurrentCallsProcedure::new();
        let req = proc.hf_update(at::Command::CindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_with_ok_response() {
        let mut proc = QueryCurrentCallsProcedure::new();
        let req = proc.hf_update(at::Command::Clcc {}, &mut SlcState::default());
        let update = match req {
            ProcedureRequest::Request(SlcRequest::QueryCurrentCalls { response }) => {
                response(vec![])
            }
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut SlcState::default());
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(msgs) if msgs == vec![at::Response::Ok]
        );
        assert!(proc.is_terminated());
    }

    #[test]
    fn procedure_with_calls_response() {
        // A terminated call will not be sent to the HF
        let terminated =
            Call::new(1, Number::from("1"), CallState::Terminated, Direction::MobileTerminated);
        // An ongoing call will be sent to the HF.
        let ongoing = Call::new(
            2,
            Number::from("2"),
            CallState::IncomingRinging,
            Direction::MobileTerminated,
        );
        let mut proc = QueryCurrentCallsProcedure::new();
        let req = proc.hf_update(at::Command::Clcc {}, &mut SlcState::default());
        let update = match req {
            ProcedureRequest::Request(SlcRequest::QueryCurrentCalls { response }) => {
                response(vec![terminated, ongoing.clone()])
            }
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut SlcState::default());

        let expected_clcc = build_clcc_response(ongoing).expect("clcc should be built");
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(msgs) if msgs == vec![expected_clcc, at::Response::Ok]
        );
        assert!(proc.is_terminated());
    }
}
