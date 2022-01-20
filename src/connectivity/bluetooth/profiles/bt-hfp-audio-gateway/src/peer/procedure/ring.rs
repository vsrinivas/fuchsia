// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use at_commands as at;

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};
use crate::peer::{
    service_level_connection::SlcState,
    update::{AgUpdate, RING_BYTES},
};

/// Represents the Ring (Alert) procedures as defined in HFP v1.8 Section 4.13 - 4.14.
///
/// The AG may send an unsolicited RING to the HF via this procedure.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
pub struct RingProcedure {
    // Whether the procedure has sent the Ring to the HF.
    responded: bool,
}

impl RingProcedure {
    pub fn new() -> Self {
        Self { responded: false }
    }
}

impl Procedure for RingProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::Ring
    }

    fn ag_update(&mut self, update: AgUpdate, state: &mut SlcState) -> ProcedureRequest {
        match update {
            update @ AgUpdate::Ring(..) if !self.responded => {
                self.responded = true;
                // Only send the CLIP if Call Line Identification Notifications are enabled for
                // the SLC.
                if state.call_line_ident_notifications {
                    update.into()
                } else {
                    vec![at::Response::RawBytes(RING_BYTES.to_vec())].into()
                }
            }
            _ => ProcedureRequest::Error(ProcedureError::UnexpectedAg(update)),
        }
    }

    /// Returns true if the Procedure is finished.
    fn is_terminated(&self) -> bool {
        self.responded
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::peer::calls::{Call, Direction};
    use {assert_matches::assert_matches, fidl_fuchsia_bluetooth_hfp::CallState};

    #[test]
    fn correct_marker() {
        let marker = RingProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::Ring);
    }

    #[test]
    fn unexpected_hf_update_returns_error() {
        let mut procedure = RingProcedure::new();
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
        let mut procedure = RingProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_ag = AgUpdate::ThreeWaySupport;
        assert_matches!(
            procedure.ag_update(random_ag, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn update_with_clip_enabled_produces_expected_request() {
        let mut procedure = RingProcedure::new();
        let mut state = SlcState { call_line_ident_notifications: true, ..SlcState::default() };

        assert!(!procedure.is_terminated());

        let call =
            Call::new(1, "123".into(), CallState::IncomingRinging, Direction::MobileTerminated);
        let update = AgUpdate::Ring(call.clone());
        let expected_messages = vec![
            at::Response::RawBytes(RING_BYTES.to_vec()),
            at::success(at::Success::Clip { ty: call.number.type_(), number: call.number.into() }),
        ];
        assert_matches!(
            procedure.ag_update(update, &mut state),
            ProcedureRequest::SendMessages(m) if m == expected_messages
        );
        assert!(procedure.is_terminated());
    }

    #[test]
    fn update_with_clip_disabled_produces_expected_request() {
        let mut procedure = RingProcedure::new();
        let mut state = SlcState { call_line_ident_notifications: false, ..SlcState::default() };

        assert!(!procedure.is_terminated());

        let call =
            Call::new(1, "123".into(), CallState::IncomingRinging, Direction::MobileTerminated);
        let update = AgUpdate::Ring(call.clone());
        let expected_messages = vec![at::Response::RawBytes(RING_BYTES.to_vec())];
        assert_matches!(
            procedure.ag_update(update, &mut state),
            ProcedureRequest::SendMessages(m) if m == expected_messages
        );
        assert!(procedure.is_terminated());
    }
}
