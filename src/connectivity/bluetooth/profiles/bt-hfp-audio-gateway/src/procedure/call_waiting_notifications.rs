// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{AgUpdate, Procedure, ProcedureError, ProcedureMarker, ProcedureRequest};

use at_commands as at;

use crate::peer::service_level_connection::SlcState;

/// The HF may disable or enable the Call Waiting Notifications via this
/// procedure. See HFP v1.8, Section 4.21.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
#[derive(Debug, Default)]
pub struct CallWaitingNotificationsProcedure {
    /// The current state of the procedure
    terminated: bool,
}

impl CallWaitingNotificationsProcedure {
    /// Create a new CallWaitingNotifications procedure in the Start state.
    pub fn new() -> Self {
        Self::default()
    }
}

impl Procedure for CallWaitingNotificationsProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::CallWaitingNotifications
    }

    fn hf_update(&mut self, update: at::Command, state: &mut SlcState) -> ProcedureRequest {
        match (self.terminated, update) {
            (false, at::Command::Ccwa { enable }) => {
                self.terminated = true;
                state.call_waiting_notifications = enable;
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
        let marker = CallWaitingNotificationsProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::CallWaitingNotifications);
    }

    #[test]
    fn procedure_handles_invalid_messages() {
        let mut proc = CallWaitingNotificationsProcedure::new();
        let req = proc.hf_update(at::Command::CopsRead {}, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedHf(_)));

        let req = proc.ag_update(AgUpdate::ThreeWaySupport, &mut SlcState::default());
        assert_matches!(req, ProcedureRequest::Error(ProcedureError::UnexpectedAg(_)));
    }

    #[test]
    fn procedure_with_response() {
        let mut proc = CallWaitingNotificationsProcedure::new();
        let req = proc.hf_update(at::Command::Ccwa { enable: true }, &mut SlcState::default());
        assert_matches!(
            req,
            ProcedureRequest::SendMessages(msgs) if msgs == vec![at::Response::Ok]
        );
        assert!(proc.is_terminated());
    }
}
