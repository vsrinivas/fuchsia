// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{service_level_connection::SlcState, slc_request::SlcRequest, update::AgUpdate};
use at_commands as at;

/// Represents the current state of the HF request to transmit an HF Indicator value as
/// defined in HFP v1.8 Section 4.36.1.5.
#[derive(Debug, PartialEq, Clone, Copy)]
enum State {
    /// Initial state of the Procedure.
    Start,
    /// A request has been received from the HF to transmit the indicator via the AG.
    SendRequest,
    /// Terminal state of the procedure.
    Terminated,
}

impl State {
    /// Transition to the next state in the procedure.
    fn transition(&mut self) {
        match *self {
            Self::Start => *self = Self::SendRequest,
            Self::SendRequest => *self = Self::Terminated,
            Self::Terminated => *self = Self::Terminated,
        }
    }
}

/// The Hf may send an updated HF Indicator value via this procedure. Defined in
/// HFP v1.8 Section 4.36.1.5.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug)]
pub struct TransferHfIndicatorProcedure {
    /// The current state of the procedure
    state: State,
}

impl Default for TransferHfIndicatorProcedure {
    fn default() -> Self {
        Self { state: State::Start }
    }
}

impl TransferHfIndicatorProcedure {
    /// Create a new Transfer HF Indicator procedure in the Start state.
    pub fn new() -> Self {
        Self::default()
    }
}

impl Procedure for TransferHfIndicatorProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::TransferHfIndicator
    }

    fn hf_update(&mut self, update: at::Command, state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::Start, at::Command::Biev { anum, value }) => {
                self.state.transition();
                // Per HFP v1.8 Section 4.36.1.5, we should send Error if the request `anum` is
                // disabled, or the `value` is out of bounds.
                if let Ok(indicator) = state.hf_indicators.update_indicator_value(anum, value) {
                    SlcRequest::SendHfIndicator { indicator, response: Box::new(|| AgUpdate::Ok) }
                        .into()
                } else {
                    self.state.transition();
                    AgUpdate::Error.into()
                }
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match (self.state, update) {
            (State::SendRequest, update @ AgUpdate::Ok) => {
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
    use crate::peer::indicators::HfIndicator;
    use assert_matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = TransferHfIndicatorProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::TransferHfIndicator);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = TransferHfIndicatorProcedure::new();
        let req = proc.hf_update(at::Command::CindRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_with_invalid_battery_value_sends_error_message() {
        let mut proc = TransferHfIndicatorProcedure::new();
        let mut state = SlcState::default();
        state.hf_indicators.enable_indicators(vec![
            at::BluetoothHFIndicator::BatteryLevel,
            at::BluetoothHFIndicator::EnhancedSafety,
        ]);

        // Battery level is not within the range [0,5].
        let cmd = at::Command::Biev { anum: at::BluetoothHFIndicator::BatteryLevel, value: 6 };
        let req = proc.hf_update(cmd, &mut state);
        let expected = vec![at::Response::Error];
        assert_matches!(req, ProcedureRequest::SendMessages(m) if m == expected);
        assert!(proc.is_terminated());
    }

    #[test]
    fn procedure_with_valid_battery_value_sends_ok() {
        let mut proc = TransferHfIndicatorProcedure::new();
        let mut state = SlcState::default();
        state.hf_indicators.enable_indicators(vec![
            at::BluetoothHFIndicator::BatteryLevel,
            at::BluetoothHFIndicator::EnhancedSafety,
        ]);

        let cmd = at::Command::Biev { anum: at::BluetoothHFIndicator::BatteryLevel, value: 1 };
        let req = proc.hf_update(cmd, &mut state);
        let update = match req {
            ProcedureRequest::Request(SlcRequest::SendHfIndicator {
                indicator: HfIndicator::BatteryLevel(1),
                response,
            }) => response(),
            x => panic!("Expected SendHFInd request but got: {:?}", x),
        };

        let req = proc.ag_update(update, &mut state);
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(msgs) if msgs == vec![at::Response::Ok]
        );
        assert!(proc.is_terminated());
    }

    #[test]
    fn procedure_with_invalid_safety_value_sends_error_message() {
        let mut proc = TransferHfIndicatorProcedure::new();
        let mut state = SlcState::default();
        state.hf_indicators.enable_indicators(vec![
            at::BluetoothHFIndicator::BatteryLevel,
            at::BluetoothHFIndicator::EnhancedSafety,
        ]);

        // Enhanced Safety can only be 0 or 1.
        let cmd = at::Command::Biev { anum: at::BluetoothHFIndicator::EnhancedSafety, value: 7 };
        let req = proc.hf_update(cmd, &mut state);
        let expected = vec![at::Response::Error];
        assert_matches!(req, ProcedureRequest::SendMessages(m) if m == expected);
        assert!(proc.is_terminated());
    }

    #[test]
    fn procedure_with_valid_safety_value_sends_ok() {
        let mut proc = TransferHfIndicatorProcedure::new();
        let mut state = SlcState::default();
        state.hf_indicators.enable_indicators(vec![
            at::BluetoothHFIndicator::BatteryLevel,
            at::BluetoothHFIndicator::EnhancedSafety,
        ]);

        let cmd = at::Command::Biev { anum: at::BluetoothHFIndicator::EnhancedSafety, value: 1 };
        let req = proc.hf_update(cmd, &mut state);
        let update = match req {
            ProcedureRequest::Request(SlcRequest::SendHfIndicator {
                indicator: HfIndicator::EnhancedSafety(true),
                response,
            }) => response(),
            x => panic!("Expected SendHFInd request but got: {:?}", x),
        };

        let req = proc.ag_update(update, &mut state);
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(msgs) if msgs == vec![at::Response::Ok]
        );
        assert!(proc.is_terminated());
    }
}
