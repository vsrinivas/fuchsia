// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{calls::CallIdx, service_level_connection::SlcState, slc_request::SlcRequest, update::AgUpdate};
use {
    at_commands as at,
    core::convert::{TryFrom, TryInto},
    std::fmt,
    std::slice::Iter,
};

// TODO (fxbug.dev/94440): Add support for multi-party calls and enhanced call control

#[derive(Debug, PartialEq, Clone, Copy)]
enum State {
    /// Initial state of the Hold Procedure.
    Start,
    /// A request has been received from the HF to hold, activate, or terminate calls.
    HoldRequest,
    /// The AG has responded to the HF's request to set the state.
    Terminated,
}

impl State {
    /// Transition to the next state in the Hold procedure.
    fn transition(&mut self) {
        match *self {
            Self::Start => *self = Self::HoldRequest,
            Self::HoldRequest => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct CallHoldActionError;

impl fmt::Display for CallHoldActionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid or unsupported call hold action")
    }
}

/// Action to perform a call related supplementary services. During a call, the following procedures
/// shall be available for the subscriber to control the operation of Call Waiting or Call Hold;
///
/// See 3GPP TS 22.030 v16.0.0 / ETSI TS 122.030 v16.0.0
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum CallHoldAction {
    /// Releases all held calls or sets User Determined User Busy (UDUB) for a waiting call.
    ReleaseAllHeld,
    /// Releases all active calls (if any exist) and accepts the other (held or waiting) call.
    ReleaseAllActive,
    /// Releases call with specified CallIdx
    ReleaseSpecified(CallIdx),
    /// Places all active calls (if any exist) on hold and accepts the other (held or waiting) call.
    HoldActiveAndAccept,
    /// Request private consultation mode with specified call (CallIdx). (Place all calls on hold
    /// EXCEPT the call indicated by CallIdx.)
    HoldAllExceptSpecified(CallIdx),
}

impl CallHoldAction {
    pub fn supported_actions() -> Iter<'static, CallHoldAction> {
        static SUPPORTED_ACTIONS: [CallHoldAction; 3] = [
            CallHoldAction::ReleaseAllHeld,
            CallHoldAction::ReleaseAllActive,
            CallHoldAction::HoldActiveAndAccept,
        ];
        SUPPORTED_ACTIONS.iter()
    }

    pub fn chld_code(&self) -> String {
        match self {
            CallHoldAction::ReleaseAllHeld => String::from("0"),
            CallHoldAction::ReleaseAllActive => String::from("1"),
            CallHoldAction::HoldActiveAndAccept => String::from("2"),
            CallHoldAction::ReleaseSpecified(idx) => format!("1{}", idx),
            CallHoldAction::HoldAllExceptSpecified(idx) => format!("2{}", idx),
        }
    }
}

impl TryFrom<&str> for CallHoldAction {
    type Error = CallHoldActionError;
    fn try_from(x: &str) -> Result<Self, Self::Error> {
        let action = match x {
            "0" => Self::ReleaseAllHeld,
            "1" => Self::ReleaseAllActive,
            "2" => Self::HoldActiveAndAccept,
            cmd if cmd.starts_with("1") => {
                let idx = cmd.strip_prefix("1").unwrap_or("").parse().map_err(|_| CallHoldActionError)?;
                Self::ReleaseSpecified(idx)
            }
            cmd if cmd.starts_with("2") => {
                let idx = cmd.strip_prefix("2").unwrap_or("").parse().map_err(|_| CallHoldActionError)?;
                Self::HoldAllExceptSpecified(idx)
            }
            _ => return Err(CallHoldActionError),
        };

        CallHoldAction::supported_actions().find(|&&x| x == action)
            .map_or(Err(CallHoldActionError), |&x| Ok(x))
    }
}

/// The HF performs supplemental services on calls via this procedure. See HFP v1.8, Section 4.22.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct HoldProcedure {
    /// The current state of the procedure
    state: State,
}

impl HoldProcedure {
    /// Create a new Hold procedure in the Start state.
    pub fn new() -> Self {
        Self { state: State::Start }
    }
}

impl Procedure for HoldProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::Hold
    }

    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, &update) {
            (State::Start, at::Command::Chld { command }) => {
                self.state.transition();
                match command.as_str().try_into() {
                    Ok(command) => {
                        let response = Box::new(Into::into);
                        SlcRequest::Hold { command, response }.into()
                    }
                    Err(_) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
                }
            }
            _ => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::HoldRequest, update @ AgUpdate::Ok)
            | (State::HoldRequest, update @ AgUpdate::Error) => {
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

/// The HF retrieves the Call Hold and Multiparty support in the AG via this procedure. See HFP
/// v1.8 Section 4.2.1.3
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct ThreeWaySupportProcedure {
    terminated: bool,
}

impl ThreeWaySupportProcedure {
    /// Create a new Hold procedure in the Start state.
    pub fn new() -> Self {
        Self { terminated: false }
    }
}

impl Procedure for ThreeWaySupportProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::ThreeWaySupport
    }

    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        match (self.terminated, &update) {
            (false, at::Command::ChldTest {}) => {
                self.terminated = true;
                AgUpdate::ThreeWaySupport.into()
            }
            _ => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
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
    fn state_transitions() {
        let mut state = State::Start;
        state.transition();
        assert_eq!(state, State::HoldRequest);
        state.transition();
        assert_eq!(state, State::Terminated);
        state.transition();
        assert_eq!(state, State::Terminated);
    }

    #[test]
    fn call_hold_action_valid_conversions() {
        assert_eq!(CallHoldAction::try_from("0"), Ok(CallHoldAction::ReleaseAllHeld));
        assert_eq!(CallHoldAction::try_from("1"), Ok(CallHoldAction::ReleaseAllActive));
        assert_eq!(CallHoldAction::try_from("2"), Ok(CallHoldAction::HoldActiveAndAccept));
    }

    #[test]
    fn call_hold_action_invalid_conversions() {
        // Empty string
        assert!(CallHoldAction::try_from("").is_err());
        // Not a number
        assert!(CallHoldAction::try_from("a").is_err());
        // Number out of range
        assert!(CallHoldAction::try_from("3").is_err());
        // Unexpected second number
        assert!(CallHoldAction::try_from("01").is_err());
        // Invalid second character on "1"
        assert!(CallHoldAction::try_from("1a").is_err());
        // Invalid second character on "2"
        assert!(CallHoldAction::try_from("2-").is_err());
        // Valid but unsupported action results in error
        assert!(CallHoldAction::try_from("19").is_err());
        // Valid but unsupported action results in error
        assert!(CallHoldAction::try_from("22").is_err());
    }

    #[test]
    fn correct_marker() {
        let marker = HoldProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::Hold);
    }

    #[test]
    fn is_terminated_in_terminated_state() {
        let mut proc = HoldProcedure::new();
        assert!(!proc.is_terminated());
        proc.state = State::HoldRequest;
        assert!(!proc.is_terminated());
        proc.state = State::Terminated;
        assert!(proc.is_terminated());
    }

    #[test]
    fn unexpected_hf_update_returns_error() {
        let mut proc = HoldProcedure::new();
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
        let mut proc = HoldProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_ag = AgUpdate::ThreeWaySupport;
        assert_matches!(
            proc.ag_update(random_ag, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn hold_produces_ok_result() {
        let mut proc = HoldProcedure::new();
        let mut state = SlcState::default();

        let req = proc.hf_update(at::Command::Chld { command: String::from("1") }, &mut state);
        let update = match req {
            ProcedureRequest::Request(SlcRequest::Hold { command, response }) => {
                assert_eq!(command, CallHoldAction::ReleaseAllActive);
                response(Ok(()))
            }
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut state);
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(resp) if resp == vec![at::Response::Ok]
        );

        // Check that the procedure is terminated and any new messages produce an error.
        assert!(proc.is_terminated());
        assert_matches!(
            proc.hf_update(at::Command::Chld { command: String::from("1") }, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(_))
        );
        assert_matches!(
            proc.ag_update(AgUpdate::Ok, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn hold_produces_err_result() {
        let mut proc = HoldProcedure::new();
        let mut state = SlcState::default();

        let req = proc.hf_update(at::Command::Chld { command: String::from("1") }, &mut state);
        let update = match req {
            ProcedureRequest::Request(SlcRequest::Hold { command, response }) => {
                assert_eq!(command, CallHoldAction::ReleaseAllActive);
                response(Err(()))
            }
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut state);
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(resp) if resp == vec![at::Response::Error]
        );

        // Check that the procedure is terminated and any new messages produce an error.
        assert!(proc.is_terminated());
        assert_matches!(
            proc.hf_update(at::Command::Chld { command: String::from("1") }, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(_))
        );
        assert_matches!(
            proc.ag_update(AgUpdate::Ok, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn invalid_hold_command_string_produces_err() {
        let mut proc = HoldProcedure::new();
        let mut state = SlcState::default();

        let req =
            proc.hf_update(at::Command::Chld { command: String::from("invalid") }, &mut state);
        assert_matches!(
            req,
            ProcedureRequest::Error(ProcedureError::UnexpectedHf(at::Command::Chld { command }))
                if command == "invalid"
        );
        assert!(!proc.is_terminated());
    }

    #[test]
    fn twc_correct_marker() {
        let marker = ThreeWaySupportProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::ThreeWaySupport);
    }

    #[test]
    fn twc_procedure_handles_invalid_messages() {
        let mut proc = ThreeWaySupportProcedure::new();
        let req = proc.hf_update(at::Command::CindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_read_command_sends_messages() {
        let mut proc = ThreeWaySupportProcedure::new();
        assert!(!proc.is_terminated());
        let req = proc.hf_update(at::Command::ChldTest {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::SendMessages(_));
        assert!(proc.is_terminated());
    }
}
