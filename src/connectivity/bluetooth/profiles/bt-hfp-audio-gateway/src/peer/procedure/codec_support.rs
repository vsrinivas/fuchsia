// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use crate::peer::{service_level_connection::SlcState, update::AgUpdate};
use at_commands as at;
use core::convert::TryFrom;

/// The HF communicates supported codecs via this procedure. See HFP v1.8, Section 4.2.1.2.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug, Default)]
pub struct CodecSupportProcedure {
    /// The current state of the procedure
    terminated: bool,
}

impl CodecSupportProcedure {
    /// Create a new CodecSupport procedure in the Start state.
    pub fn new() -> Self {
        Self::default()
    }
}

impl Procedure for CodecSupportProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::CodecSupport
    }

    fn hf_update(&mut self, update: at::Command, state: &mut SlcState) -> ProcedureRequest {
        match (self.terminated, update) {
            (false, at::Command::Bac { codecs }) => {
                self.terminated = true;
                state.hf_supported_codecs = Some(
                    codecs
                        .into_iter()
                        .filter_map(|x| u8::try_from(x).ok().map(Into::into))
                        .collect(),
                );
                AgUpdate::Ok.into()
            }
            (_, update) => ProcedureRequest::Error(ProcedureError::UnexpectedHf(update)),
        }
    }

    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = CodecSupportProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::CodecSupport);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = CodecSupportProcedure::new();
        let req = proc.hf_update(at::Command::CopsRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_with_ok_response() {
        let mut proc = CodecSupportProcedure::new();
        let req = proc.hf_update(at::Command::Bac { codecs: vec![1] }, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::SendMessages(_));
        assert!(proc.is_terminated());
    }
}
