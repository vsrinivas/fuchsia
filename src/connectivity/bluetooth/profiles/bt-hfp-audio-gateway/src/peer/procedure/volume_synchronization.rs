// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use {at_commands as at, core::convert::TryInto};

use crate::peer::{service_level_connection::SlcState, slc_request::SlcRequest, update::AgUpdate};

/// Represents the current state of the Hf procedure to report its volume level as defined in
/// HFP v1.8, Section 4.29.2.
#[derive(Debug, PartialEq, Clone, Copy)]
enum State {
    /// Initial state of the Procedure.
    Start,
    /// A report has been received from the HF containing its volume level.
    SetRequest,
    /// Terminal state of the procedure.
    Terminated,
}

impl State {
    /// Transition to the next state in the VolumeSynchronization procedure.
    fn transition(&mut self) {
        match *self {
            Self::Start => *self = Self::SetRequest,
            Self::SetRequest => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

/// The HF may report its speaker and microphone volume levels via this procedure. See HFP v1.8,
/// Section 4.29.2.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct VolumeSynchronizationProcedure {
    /// The current state of the procedure
    state: State,
}

impl Default for VolumeSynchronizationProcedure {
    fn default() -> Self {
        Self { state: State::Start }
    }
}

impl VolumeSynchronizationProcedure {
    /// Create a new VolumeSynchronization procedure in the Start state.
    pub fn new() -> Self {
        Self { state: State::Start }
    }
}

impl Procedure for VolumeSynchronizationProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::VolumeSynchronization
    }

    // TODO (fxbug.dev/72681): This procedure is a good example of some room for improvement in the
    // procedure design. See bug for details.
    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, &update) {
            (State::Start, at::Command::Vgs { level }) => {
                self.state.transition();
                match (*level).try_into() {
                    Ok(level) => SlcRequest::SpeakerVolumeSynchronization {
                        level,
                        response: Box::new(|| AgUpdate::Ok),
                    }
                    .into(),
                    Err(_) => ProcedureRequest::Error(ProcedureError::InvalidHfArgument(update)),
                }
            }
            (State::Start, at::Command::Vgm { level }) => {
                self.state.transition();
                match (*level).try_into() {
                    Ok(level) => SlcRequest::MicrophoneVolumeSynchronization {
                        level,
                        response: Box::new(|| AgUpdate::Ok),
                    }
                    .into(),
                    Err(_) => ProcedureRequest::Error(ProcedureError::InvalidHfArgument(update)),
                }
            }
            _ => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::SetRequest, update @ AgUpdate::Ok) => {
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
    use std::convert::TryInto;

    #[test]
    fn correct_marker() {
        let marker = VolumeSynchronizationProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::VolumeSynchronization);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = VolumeSynchronizationProcedure::new();
        let req = proc.hf_update(at::Command::CindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_handles_invalid_level_values() {
        let mut proc = VolumeSynchronizationProcedure::new();
        let req = proc.hf_update(at::Command::Vgs { level: 16 }, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::InvalidHfArgument(_)));

        let mut proc = VolumeSynchronizationProcedure::new();
        let req = proc.hf_update(at::Command::Vgm { level: 16 }, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::InvalidHfArgument(_)));
    }

    #[test]
    fn procedure_with_speaker_gain() {
        let mut proc = VolumeSynchronizationProcedure::new();
        let req = proc.hf_update(at::Command::Vgs { level: 1 }, &mut SlcState::default());
        let update = match req {
            ProcedureRequest::Request(SlcRequest::SpeakerVolumeSynchronization {
                level,
                response,
            }) if level == 1u8.try_into().unwrap() => response(),
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
    fn procedure_with_microphone_gain() {
        let mut proc = VolumeSynchronizationProcedure::new();
        let req = proc.hf_update(at::Command::Vgm { level: 1 }, &mut SlcState::default());
        let update = match req {
            ProcedureRequest::Request(SlcRequest::MicrophoneVolumeSynchronization {
                level,
                response,
            }) if level == 1u8.try_into().unwrap() => response(),
            x => panic!("Unexpected message: {:?}", x),
        };
        let req = proc.ag_update(update, &mut SlcState::default());
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(msgs) if msgs == vec![at::Response::Ok]
        );
        assert!(proc.is_terminated());
    }
}
