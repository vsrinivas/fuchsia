// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};
use crate::peer::{service_level_connection::SlcState, update::AgUpdate};

/// Represents the speaker volume section of the Audio Volume Control procedure as defined in
/// HFP v1.8 Section 4.29.1
///
/// Section 4.29.1 defines two separate unsolicited responses that may be sent in any order or only
/// one at a time.  This procedure handles the +VGS response, which is used to set the speaker volume
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.

pub struct VolumeControlProcedure {
    // Whether the procedure has sent the volume to the HF.
    responded: bool,
}

impl VolumeControlProcedure {
    pub fn new() -> Self {
        Self { responded: false }
    }
}

impl Procedure for VolumeControlProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::VolumeControl
    }

    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        match update {
            AgUpdate::SpeakerVolumeControl(_) | AgUpdate::MicrophoneVolumeControl(_)
                if !self.responded =>
            {
                self.responded = true;
                update.into()
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
    use crate::peer::gain_control::Gain;
    use assert_matches::assert_matches;
    use at_commands as at;
    use std::convert::TryFrom;

    #[test]
    fn correct_marker() {
        let marker = VolumeControlProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::VolumeControl);
    }

    #[test]
    fn unexpected_hf_update_returns_error() {
        let mut procedure = VolumeControlProcedure::new();
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
        let mut procedure = VolumeControlProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_ag = AgUpdate::ThreeWaySupport;
        assert_matches!(
            procedure.ag_update(random_ag, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn speaker_update_produces_expected_request() {
        let mut procedure = VolumeControlProcedure::new();
        let mut state = SlcState::default();

        assert!(!procedure.is_terminated());

        let gain = Gain::try_from(4i64).unwrap(); // Can't fail since it's withing the range for valid gains.
        let update = AgUpdate::SpeakerVolumeControl(gain);
        let expected_messages = vec![at::Response::Success(at::Success::Vgs { level: 4i64 })];
        assert_matches!(procedure.ag_update(update, &mut state), ProcedureRequest::SendMessages(m) if m == expected_messages);
        assert!(procedure.is_terminated());
    }

    #[test]
    fn microphonbe_update_produces_expected_request() {
        let mut procedure = VolumeControlProcedure::new();
        let mut state = SlcState::default();

        assert!(!procedure.is_terminated());

        let gain = Gain::try_from(4i64).unwrap(); // Can't fail since it's withing the range for valid gains.
        let update = AgUpdate::MicrophoneVolumeControl(gain);
        let expected_messages = vec![at::Response::Success(at::Success::Vgm { level: 4i64 })];
        assert_matches!(procedure.ag_update(update, &mut state), ProcedureRequest::SendMessages(m) if m == expected_messages);
        assert!(procedure.is_terminated());
    }
}
