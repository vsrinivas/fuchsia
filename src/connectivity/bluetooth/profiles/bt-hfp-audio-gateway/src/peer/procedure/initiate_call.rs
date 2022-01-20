// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{
    calls::CallAction, service_level_connection::SlcState, slc_request::SlcRequest,
    update::AgUpdate,
};
use at_commands as at;

/// Represents the current state of the Hf request to initiate call, as defined in HFP v1.8 4.18-4.20.
#[derive(Debug, PartialEq, Clone, Copy)]
enum State {
    /// Initial state of the Initiate Call Procedure.
    Start,
    /// A request has been received from the HF to initiate a call.
    CallRequested,
    /// The AG has responded to the HF's request to set the state.
    Terminated,
}

impl State {
    /// Transition to the next state in the procedure.
    fn transition(&mut self) {
        match *self {
            Self::Start => *self = Self::CallRequested,
            Self::CallRequested => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

#[derive(Debug)]
pub struct InitiateCallProcedure {
    /// The current state of the procedure
    state: State,
}

impl InitiateCallProcedure {
    /// Create a new procedure in the Start state.
    pub fn new() -> Self {
        Self { state: State::Start }
    }
}

impl Procedure for InitiateCallProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::InitiateCall
    }

    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (
                State::Start,
                // `update` above is consumed by this pattern match, so rebind it.
                update @ (at::Command::AtdNumber { .. }
                | at::Command::AtdMemory { .. }
                | at::Command::Bldn { .. }),
            ) => {
                self.state.transition();
                let response = Box::new(|res: Result<(), ()>| {
                    res.map(|()| AgUpdate::Ok).unwrap_or(AgUpdate::Error)
                });
                match update_to_information_request(update, response) {
                    Ok(information_request) => ProcedureRequest::Request(information_request),
                    Err(update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
                }
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::CallRequested, update @ AgUpdate::Ok)
            | (State::CallRequested, update @ AgUpdate::Error) => {
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

/// Callers must guarantee this is a form of ATD or AT+BLDN.
fn update_to_information_request(
    update: at::Command,
    response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate>,
) -> Result<SlcRequest, at::Command> {
    let call_action_result = match update {
        at::Command::AtdNumber { number } => Ok(CallAction::InitiateByNumber(number)),
        at::Command::AtdMemory { location } => Ok(CallAction::InitiateByMemoryLocation(location)),
        at::Command::Bldn {} => Ok(CallAction::InitiateByRedialLast),
        _ => Err(update),
    };
    call_action_result.map(|call_action| SlcRequest::InitiateCall { call_action, response })
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn state_transitions() {
        let mut state = State::Start;
        state.transition();
        assert_eq!(state, State::CallRequested);
        state.transition();
        assert_eq!(state, State::Terminated);
        state.transition();
        assert_eq!(state, State::Terminated);
    }

    #[test]
    fn correct_marker() {
        let marker = InitiateCallProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::InitiateCall);
    }

    #[test]
    fn is_terminated_in_terminated_state() {
        let mut proc = InitiateCallProcedure::new();
        assert!(!proc.is_terminated());
        proc.state = State::CallRequested;
        assert!(!proc.is_terminated());
        proc.state = State::Terminated;
        assert!(proc.is_terminated());
    }

    #[test]
    fn unexpected_hf_update_returns_error() {
        let mut proc = InitiateCallProcedure::new();
        let mut state = SlcState::default();

        // SLCI AT command.
        let random_hf = at::Command::CindRead {};
        assert_matches!(
            proc.hf_update(random_hf, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(_))
        );
    }

    #[test]
    fn unexpected_ag_update_returns_error() {
        let mut proc = InitiateCallProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_ag = AgUpdate::ThreeWaySupport;
        assert_matches!(
            proc.ag_update(random_ag, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn atd_number_updates_produce_expected_requests() {
        let mut proc = InitiateCallProcedure::new();
        let mut state = SlcState::default();

        let number = String::from("a phone number");

        let req = proc.hf_update(at::Command::AtdNumber { number: number.clone() }, &mut state);
        let update = match req {
            ProcedureRequest::Request(SlcRequest::InitiateCall {
                call_action: CallAction::InitiateByNumber(_),
                response,
            }) => response(Ok(())),
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut state);
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(resp) if resp == vec![at::Response::Ok]
        );

        // Check that the procedure is termianted and any new messages produce an error.
        assert!(proc.is_terminated());
        assert_matches!(
            proc.hf_update(at::Command::AtdNumber { number }, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(_))
        );
        assert_matches!(
            proc.ag_update(AgUpdate::Ok, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn atd_memory_updates_produce_expected_requests() {
        let mut proc = InitiateCallProcedure::new();
        let mut state = SlcState::default();

        let location = String::from("a location");

        let req = proc.hf_update(at::Command::AtdMemory { location: location.clone() }, &mut state);
        let update = match req {
            ProcedureRequest::Request(SlcRequest::InitiateCall {
                call_action: CallAction::InitiateByMemoryLocation(_),
                response,
            }) => response(Ok(())),
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut state);
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(resp) if resp == vec![at::Response::Ok]
        );

        // Check that the procedure is termianted and any new messages produce an error.
        assert!(proc.is_terminated());
        assert_matches!(
            proc.hf_update(at::Command::AtdMemory { location }, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(_))
        );
        assert_matches!(
            proc.ag_update(AgUpdate::Ok, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn bldn_updates_produce_expected_requests() {
        let mut proc = InitiateCallProcedure::new();
        let mut state = SlcState::default();

        let req = proc.hf_update(at::Command::Bldn {}, &mut state);
        let update = match req {
            ProcedureRequest::Request(SlcRequest::InitiateCall {
                call_action: CallAction::InitiateByRedialLast,
                response,
            }) => response(Ok(())),
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut state);
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(resp) if resp == vec![at::Response::Ok]
        );

        // Check that the procedure is termianted and any new messages produce an error.
        assert!(proc.is_terminated());
        assert_matches!(
            proc.hf_update(at::Command::Bldn {}, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(_))
        );
        assert_matches!(
            proc.ag_update(AgUpdate::Ok, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }
}
