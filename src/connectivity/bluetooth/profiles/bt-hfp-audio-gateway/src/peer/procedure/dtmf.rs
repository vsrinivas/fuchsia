// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{service_level_connection::SlcState, slc_request::SlcRequest, update::AgUpdate};

use {
    at_commands as at,
    core::convert::{TryFrom, TryInto},
    fidl_fuchsia_bluetooth_hfp as fidl,
};

/// Represents a single Dual-tone multi-requency signaling code.
/// This is a native representation of the FIDL enum `fuchsia.bluetooth.hfp.DtmfCode`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum DtmfCode {
    One,
    Two,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    NumberSign,
    Zero,
    Asterisk,
    A,
    B,
    C,
    D,
}

impl TryFrom<&str> for DtmfCode {
    type Error = ();
    fn try_from(x: &str) -> Result<Self, Self::Error> {
        match x {
            "1" => Ok(Self::One),
            "2" => Ok(Self::Two),
            "3" => Ok(Self::Three),
            "4" => Ok(Self::Four),
            "5" => Ok(Self::Five),
            "6" => Ok(Self::Six),
            "7" => Ok(Self::Seven),
            "8" => Ok(Self::Eight),
            "9" => Ok(Self::Nine),
            "#" => Ok(Self::NumberSign),
            "0" => Ok(Self::Zero),
            "*" => Ok(Self::Asterisk),
            "A" => Ok(Self::A),
            "B" => Ok(Self::B),
            "C" => Ok(Self::C),
            "D" => Ok(Self::D),
            _ => Err(()),
        }
    }
}

impl From<fidl::DtmfCode> for DtmfCode {
    fn from(x: fidl::DtmfCode) -> Self {
        match x {
            fidl::DtmfCode::One => Self::One,
            fidl::DtmfCode::Two => Self::Two,
            fidl::DtmfCode::Three => Self::Three,
            fidl::DtmfCode::Four => Self::Four,
            fidl::DtmfCode::Five => Self::Five,
            fidl::DtmfCode::Six => Self::Six,
            fidl::DtmfCode::Seven => Self::Seven,
            fidl::DtmfCode::Eight => Self::Eight,
            fidl::DtmfCode::Nine => Self::Nine,
            fidl::DtmfCode::NumberSign => Self::NumberSign,
            fidl::DtmfCode::Zero => Self::Zero,
            fidl::DtmfCode::Asterisk => Self::Asterisk,
            fidl::DtmfCode::A => Self::A,
            fidl::DtmfCode::B => Self::B,
            fidl::DtmfCode::C => Self::C,
            fidl::DtmfCode::D => Self::D,
        }
    }
}

impl From<DtmfCode> for fidl::DtmfCode {
    fn from(x: DtmfCode) -> Self {
        match x {
            DtmfCode::One => Self::One,
            DtmfCode::Two => Self::Two,
            DtmfCode::Three => Self::Three,
            DtmfCode::Four => Self::Four,
            DtmfCode::Five => Self::Five,
            DtmfCode::Six => Self::Six,
            DtmfCode::Seven => Self::Seven,
            DtmfCode::Eight => Self::Eight,
            DtmfCode::Nine => Self::Nine,
            DtmfCode::NumberSign => Self::NumberSign,
            DtmfCode::Zero => Self::Zero,
            DtmfCode::Asterisk => Self::Asterisk,
            DtmfCode::A => Self::A,
            DtmfCode::B => Self::B,
            DtmfCode::C => Self::C,
            DtmfCode::D => Self::D,
        }
    }
}

/// Represents the current state of the HF request to transmit a DTMF Code as defined in HFP v1.8,
/// Section 4.28.
#[derive(Debug, PartialEq, Clone, Copy)]
enum State {
    /// Initial state of the Procedure.
    Start,
    /// A request has been received from the HF to transmit a DTMF Code via the Audio Gateway.
    SendRequest,
    /// Terminal state of the procedure.
    Terminated,
}

impl State {
    /// Transition to the next state in the Dtmf procedure.
    fn transition(&mut self) {
        match *self {
            Self::Start => *self = Self::SendRequest,
            Self::SendRequest => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

/// The HF may transmit DTMF Codes via this procedure. See HFP v1.8, Section 4.28.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct DtmfProcedure {
    /// The current state of the procedure
    state: State,
}

impl Default for DtmfProcedure {
    fn default() -> Self {
        Self { state: State::Start }
    }
}

impl DtmfProcedure {
    /// Create a new Dtmf procedure in the Start state.
    pub fn new() -> Self {
        Self { state: State::Start }
    }
}

impl Procedure for DtmfProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::Dtmf
    }

    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, &update) {
            (State::Start, at::Command::Vts { code }) => {
                self.state.transition();
                match code.as_str().try_into() {
                    Ok(code) => {
                        let response = Box::new(Into::into);
                        SlcRequest::SendDtmf { code, response }.into()
                    }
                    Err(()) => ProcedureRequest::Error(ProcedureError::InvalidHfArgument(update)),
                }
            }
            _ => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::SendRequest, update @ (AgUpdate::Ok | AgUpdate::Error)) => {
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

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = DtmfProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::Dtmf);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = DtmfProcedure::new();
        let req = proc.hf_update(at::Command::CindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_with_invalid_dtmf_code() {
        let mut proc = DtmfProcedure::new();
        let req = proc.hf_update(at::Command::Vts { code: "foo".into() }, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::InvalidHfArgument(_)));
    }

    #[test]
    fn procedure_with_ok_response() {
        let mut proc = DtmfProcedure::new();
        let req = proc.hf_update(at::Command::Vts { code: "1".into() }, &mut SlcState::default());
        let update = match req {
            ProcedureRequest::Request(SlcRequest::SendDtmf { code: DtmfCode::One, response }) => {
                response(Ok(()))
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
    fn procedure_with_err_response() {
        let mut proc = DtmfProcedure::new();
        let req = proc.hf_update(at::Command::Vts { code: "1".into() }, &mut SlcState::default());
        let update = match req {
            ProcedureRequest::Request(SlcRequest::SendDtmf { code: DtmfCode::One, response }) => {
                response(Err(()))
            }
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut SlcState::default());
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(msgs) if msgs == vec![at::Response::Error]
        );
        assert!(proc.is_terminated());
    }
}
