// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use at_commands as at;

use crate::peer::{service_level_connection::SlcState, slc_request::SlcRequest, update::AgUpdate};

/// Represents the current state of the HF request to get the Subscriber Number Information
/// as defined in HFP v1.8, Section 4.31.
#[derive(Debug, PartialEq, Clone, Copy)]
enum State {
    /// Initial state of the Procedure.
    Start,
    /// A request has been received from the HF to get the Subscriber Number Information.
    GetRequest,
    /// Terminal state of the procedure.
    Terminated,
}

impl State {
    /// Transition to the next state in the SubscriberNumberInformation procedure.
    fn transition(&mut self) {
        match *self {
            Self::Start => *self = Self::GetRequest,
            Self::GetRequest => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

/// The HF may retrieve the AG Subscriber Number Information via this
/// procedure. See HFP v1.8, Section 4.31.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct SubscriberNumberInformationProcedure {
    /// The current state of the procedure
    state: State,
}

impl Default for SubscriberNumberInformationProcedure {
    fn default() -> Self {
        Self { state: State::Start }
    }
}

impl SubscriberNumberInformationProcedure {
    /// Create a new SubscriberNumberInformation procedure in the Start state.
    pub fn new() -> Self {
        Self { state: State::Start }
    }
}

impl Procedure for SubscriberNumberInformationProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::SubscriberNumberInformation
    }

    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::Start, at::Command::Cnum {}) => {
                self.state.transition();
                let response = Box::new(|numbers| AgUpdate::SubscriberNumbers(numbers));
                SlcRequest::GetSubscriberNumberInformation { response }.into()
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::GetRequest, update @ AgUpdate::SubscriberNumbers(..)) => {
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

/// Build an AT CNUM response from a number. See HFP v1.8, Section 4.34.2 for information on the
/// +CNUM fields.
pub fn build_cnum_response(number: String) -> at::Response {
    at::success(at::Success::Cnum {
        // Unsupported, shall be left blank.
        alpha: "".into(),
        number,
        // Format: no changes to the number presentation required
        ty: 128,
        // Unsupported, shall be left blank.
        speed: "".into(),
        // Indicates voice service.
        service: 4,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = SubscriberNumberInformationProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::SubscriberNumberInformation);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = SubscriberNumberInformationProcedure::new();
        let req = proc.hf_update(at::Command::CindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_with_ok_response() {
        let mut proc = SubscriberNumberInformationProcedure::new();
        let req = proc.hf_update(at::Command::Cnum {}, &mut SlcState::default());
        let update = match req {
            ProcedureRequest::Request(SlcRequest::GetSubscriberNumberInformation { response }) => {
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
    fn procedure_with_numbers_response() {
        let mut proc = SubscriberNumberInformationProcedure::new();
        let req = proc.hf_update(at::Command::Cnum {}, &mut SlcState::default());
        let number = String::from("1234567");
        let update = match req {
            ProcedureRequest::Request(SlcRequest::GetSubscriberNumberInformation { response }) => {
                response(vec![number.clone()])
            }
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut SlcState::default());
        let cnum_response = build_cnum_response(number);
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(msgs) if msgs == vec![cnum_response, at::Response::Ok]
        );
        assert!(proc.is_terminated());
    }
}
