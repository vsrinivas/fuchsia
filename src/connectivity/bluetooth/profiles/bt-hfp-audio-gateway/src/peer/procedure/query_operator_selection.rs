// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{service_level_connection::SlcState, slc_request::SlcRequest, update::AgUpdate};

use {at_commands as at, tracing::info};

/// The maximum number of characters of a long alphanumeric name.
/// Defined in HFP v1.9, Section 4.8.
pub const MAX_LONG_ALPHANUMERIC_NAME_SIZE: usize = 16;

/// Formats the provided `name` to conform to the current network operator format.
pub fn format_operator_name(format: at::NetworkOperatorNameFormat, mut name: String) -> String {
    match format {
        at::NetworkOperatorNameFormat::LongAlphanumeric => {
            if name.len() > MAX_LONG_ALPHANUMERIC_NAME_SIZE {
                let full_name = name.clone();
                name.truncate(MAX_LONG_ALPHANUMERIC_NAME_SIZE);
                info!("Truncating network operator name \"{}\" to \"{}\"", full_name, name);
            }
        }
    }
    name
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum State {
    /// Initial state of the procedure.
    Start,
    /// A request has been received from the HF to set the network operator format.
    SetFormatRequest,
    /// A request has been received from the HF to send the current Network Name.
    GetName,
    /// The AG has responded to the HF's request with the current Network Name and the procedure
    /// is complete.
    Terminated,
}

impl State {
    fn is_start(&self) -> bool {
        if let Self::Start = self {
            true
        } else {
            false
        }
    }
    /// Transition to the next state in the QOS procedure.
    /// If `skip_set_format` is set, the transition will skip the `SetFormat` state.
    fn transition(&mut self, skip_set_format: bool) {
        match *self {
            Self::Start if skip_set_format => *self = Self::GetName,
            Self::Start => *self = Self::SetFormatRequest,
            Self::SetFormatRequest => *self = Self::GetName,
            Self::GetName => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

/// Represents the Query Operator Selection procedure as defined in HFP v1.8 Section 4.8.
///
/// The HF may request the name of the currently selected Network Operator in the AG
/// via this procedure.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
pub struct QueryOperatorProcedure {
    state: State,
}

impl QueryOperatorProcedure {
    pub fn new() -> Self {
        Self { state: State::Start }
    }
}

impl Procedure for QueryOperatorProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::QueryOperatorSelection
    }

    fn hf_update(&mut self, update: at::Command, state: &mut SlcState) -> ProcedureRequest {
        // This format is required to be used by the spec, so it's the default if none is set.
        let format = state
            .ag_network_operator_name_format
            .unwrap_or(at::NetworkOperatorNameFormat::LongAlphanumeric);
        match (self.state, update) {
            (State::Start, at::Command::Cops { three: _, format }) => {
                // The remote peer has requested to set the network name format.
                state.ag_network_operator_name_format = Some(format);
                self.state.transition(/* skip_set_format= */ false);
                AgUpdate::Ok.into()
            }
            (State::Start, at::Command::CopsRead {})
            | (State::SetFormatRequest, at::Command::CopsRead {}) => {
                self.state.transition(/* skip_set_format= */ self.state.is_start());
                let response = Box::new(move |network_name: Option<_>| {
                    let name =
                        format_operator_name(format, network_name.unwrap_or_else(String::new));
                    AgUpdate::NetworkOperatorName(format, name)
                });
                SlcRequest::GetNetworkOperatorName { response }.into()
            }
            (State::Terminated, at::Command::Cops { .. })
            | (State::Terminated, at::Command::CopsRead {}) => {
                ProcedureRequest::Error(ProcedureError::AlreadyTerminated)
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::GetName, update @ AgUpdate::NetworkOperatorName(..)) => {
                self.state.transition(/* skip_set_format= */ false);
                update.into()
            }
            (State::Terminated, AgUpdate::NetworkOperatorName(..)) => {
                ProcedureRequest::Error(ProcedureError::AlreadyTerminated)
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedAg(update)),
        }
    }

    /// Returns true if the Procedure is finished.
    fn is_terminated(&self) -> bool {
        self.state == State::Terminated
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn state_transitions() {
        let mut state = State::Start;
        state.transition(/* skip_set_format= */ false);
        assert_eq!(state, State::SetFormatRequest);
        state.transition(/* skip_set_format= */ false);
        assert_eq!(state, State::GetName);
        state.transition(/* skip_set_format= */ false);
        assert_eq!(state, State::Terminated);
        state.transition(/* skip_set_format= */ false);
        assert_eq!(state, State::Terminated);
    }

    #[test]
    fn state_transition_when_skipping_set_format() {
        let mut state = State::Start;
        state.transition(/* skip_set_format= */ true);
        assert_eq!(state, State::GetName);
        state.transition(/* skip_set_format= */ false);
        assert_eq!(state, State::Terminated);
        state.transition(/* skip_set_format= */ false);
        assert_eq!(state, State::Terminated);
    }

    #[test]
    fn correct_marker() {
        let marker = QueryOperatorProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::QueryOperatorSelection);
    }

    #[test]
    fn is_terminated_in_terminated_state() {
        let mut proc = QueryOperatorProcedure::new();
        assert!(!proc.is_terminated());
        proc.state = State::SetFormatRequest;
        assert!(!proc.is_terminated());
        proc.state = State::GetName;
        assert!(!proc.is_terminated());
        proc.state = State::Terminated;
        assert!(proc.is_terminated());
    }

    #[test]
    fn unexpected_hf_update_returns_error() {
        let mut procedure = QueryOperatorProcedure::new();
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
        let mut procedure = QueryOperatorProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_ag = AgUpdate::ThreeWaySupport;
        assert_matches!(
            procedure.ag_update(random_ag, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn updates_produce_expected_requests() {
        let mut p = QueryOperatorProcedure::new();
        let test_operator_name = Some("Foobar".to_string());
        let mut state = SlcState::default();

        // The HF request to set the format should update the shared state.
        let expected_format = at::NetworkOperatorNameFormat::LongAlphanumeric;
        let update1 = at::Command::Cops { three: 3, format: expected_format };
        assert_matches!(p.hf_update(update1, &mut state), ProcedureRequest::SendMessages(_));
        assert_eq!(state.ag_network_operator_name_format, Some(expected_format));

        let update2 = at::Command::CopsRead {};
        let update3 = match p.hf_update(update2, &mut state) {
            ProcedureRequest::Request(SlcRequest::GetNetworkOperatorName { response }) => {
                response(test_operator_name)
            }
            x => {
                panic!("Expected get network operator request but got: {:?}", x);
            }
        };

        assert_matches!(p.ag_update(update3, &mut state), ProcedureRequest::SendMessages(_));

        // Check that the procedure is terminated and any new messages produce an error.
        assert!(p.is_terminated());
        assert_matches!(
            p.hf_update(
                at::Command::Cops {
                    three: 3,
                    format: at::NetworkOperatorNameFormat::LongAlphanumeric
                },
                &mut state
            ),
            ProcedureRequest::Error(ProcedureError::AlreadyTerminated)
        );
        assert_matches!(
            p.ag_update(
                AgUpdate::NetworkOperatorName(
                    at::NetworkOperatorNameFormat::LongAlphanumeric,
                    "foo".into()
                ),
                &mut state
            ),
            ProcedureRequest::Error(ProcedureError::AlreadyTerminated)
        );
    }

    #[test]
    fn updates_when_skipping_set_format_produce_expected_requests() {
        let mut p = QueryOperatorProcedure::new();
        let mut state = SlcState::default();
        let test_operator_name = Some("Bar".to_string());

        let update1 = at::Command::CopsRead {};
        let update2 = match p.hf_update(update1, &mut state) {
            ProcedureRequest::Request(SlcRequest::GetNetworkOperatorName { response }) => {
                response(test_operator_name)
            }
            x => panic!("Expected get network operator request but got: {:?}", x),
        };
        assert_matches!(p.ag_update(update2, &mut state), ProcedureRequest::SendMessages(_));
        assert!(p.is_terminated());
    }

    #[test]
    fn update_with_empty_name_produces_expected_requests() {
        let mut p = QueryOperatorProcedure::new();
        let mut state = SlcState::default();
        let test_operator_name = None;

        let update1 = at::Command::CopsRead {};
        let update2 = match p.hf_update(update1, &mut state) {
            ProcedureRequest::Request(SlcRequest::GetNetworkOperatorName { response }) => {
                response(test_operator_name)
            }
            x => panic!("Expected get network operator request but got: {:?}", x),
        };
        assert_matches!(p.ag_update(update2, &mut state), ProcedureRequest::SendMessages(_));
        assert!(p.is_terminated());
    }
}
