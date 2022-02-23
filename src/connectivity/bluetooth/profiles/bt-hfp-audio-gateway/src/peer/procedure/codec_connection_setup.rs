// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{AgUpdate, Procedure, ProcedureError, ProcedureMarker, ProcedureRequest, SlcRequest};

use crate::features::CodecId;
use crate::peer::service_level_connection::SlcState;

use at_commands as at;
use std::mem;
use tracing::warn;

#[derive(Debug, Clone, PartialEq)]
pub enum CodecConnectionSetupProcedure {
    /// Inital State of the Codec Setup Procedure
    Start,
    /// Request has been made to the HF to set the codec id.
    RequestCodec { codec: CodecId },
    /// If the codec has been changed, we need to send an extra OK to acknowledge this.
    SynchronousConnectionRequestReply { codec_changed: bool },
    /// We are waiting for the result from the SCO setup.
    SynchronousConnectionSetup,
    /// Completed. The codec connection (over SCO) should be setup now if result is Ok(())
    Terminated(Result<(), ()>),
}

impl CodecConnectionSetupProcedure {
    pub fn new() -> Self {
        Self::Start
    }
}

fn setup_request() -> ProcedureRequest {
    let response = Box::new(Into::into);
    SlcRequest::SynchronousConnectionSetup { response }.into()
}

fn select_codec(supported: Vec<CodecId>) -> CodecId {
    // Prefer SBD over CVSD if it is supported, as it's higher quality.
    if supported.contains(&CodecId::MSBC) {
        // TODO(fxbug.dev/81374): Re-enable MSBC when it's working again.
        // CodecId::MSBC
        CodecId::CVSD
    } else {
        CodecId::CVSD
    }
}

impl Procedure for CodecConnectionSetupProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::CodecConnectionSetup
    }

    fn hf_update(&mut self, update: at::Command, slc_state: &mut SlcState) -> ProcedureRequest {
        let state = mem::replace(self, Self::Terminated(Err(())));
        let codec = select_codec(slc_state.codecs_supported());

        if state == Self::Start {
            slc_state.codec_connection_setup_in_progress = true;
        }

        match (state, update) {
            (Self::Start, at::Command::Bcc {}) if Some(codec) != slc_state.selected_codec => {
                *self = Self::RequestCodec { codec };
                vec![at::Response::Ok, at::success(at::Success::Bcs { codec: codec.into() })].into()
            }
            (Self::Start, at::Command::Bcc {}) if Some(codec) == slc_state.selected_codec => {
                *self = Self::SynchronousConnectionRequestReply { codec_changed: false };
                setup_request()
            }
            (Self::RequestCodec { codec: requested }, at::Command::Bcs { codec: confirmed })
                if requested == confirmed =>
            {
                *self = Self::SynchronousConnectionRequestReply { codec_changed: true };
                slc_state.selected_codec = Some(codec);
                setup_request()
            }
            (state, update) => {
                warn!("Got unexpected update for {:?}: {:?}", state, update);
                slc_state.codec_connection_setup_in_progress = false;
                ProcedureRequest::Error(ProcedureError::UnexpectedHf(update))
            }
        }
    }

    fn ag_update(&mut self, update: AgUpdate, slc_state: &mut SlcState) -> ProcedureRequest {
        let state = mem::replace(self, Self::Terminated(Err(())));
        let selected = select_codec(slc_state.codecs_supported());

        if state == Self::Start {
            slc_state.codec_connection_setup_in_progress = true;
        }

        match (state, update) {
            // Allow this prodedure to be restarted at any point.
            // We shouldn't have a codec selected on startup of this.
            (_, AgUpdate::CodecSetup(None)) => {
                if slc_state.codec_negotiation() && Some(selected) != slc_state.selected_codec {
                    *self = Self::RequestCodec { codec: selected };
                    AgUpdate::CodecSetup(Some(selected)).into()
                } else {
                    // The HF doesn't support Codec Negotiation or we don't need to change the selected codec
                    // Just try to setup the connection
                    *self = Self::SynchronousConnectionRequestReply { codec_changed: false };
                    setup_request()
                }
            }
            (Self::SynchronousConnectionRequestReply { codec_changed }, AgUpdate::Ok) => {
                *self = Self::SynchronousConnectionSetup;
                if codec_changed {
                    AgUpdate::Ok.into()
                } else {
                    ProcedureRequest::None
                }
            }
            (Self::SynchronousConnectionSetup, AgUpdate::Ok) => {
                *self = Self::Terminated(Ok(()));
                slc_state.codec_connection_setup_in_progress = false;
                ProcedureRequest::None
            }
            (state, update) => {
                warn!("Got unexpected update for {:?}: {:?}", state, update);
                slc_state.codec_connection_setup_in_progress = false;
                ProcedureRequest::Error(ProcedureError::UnexpectedAg(update))
            }
        }
    }

    fn is_terminated(&self) -> bool {
        matches!(self, Self::Terminated(_))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::features::{AgFeatures, HfFeatures};
    use assert_matches::assert_matches;

    #[test]
    fn correct_marker() {
        let marker = CodecConnectionSetupProcedure::new().marker();
        assert_eq!(marker, ProcedureMarker::CodecConnectionSetup);
    }

    #[test]
    fn skips_codec_negotiation_if_not_supported() {
        let mut slc_state = SlcState::default();
        let mut procedure = CodecConnectionSetupProcedure::new();
        let request = procedure.ag_update(AgUpdate::CodecSetup(None), &mut slc_state);
        assert_matches!(
            request,
            ProcedureRequest::Request(SlcRequest::SynchronousConnectionSetup { .. })
        );

        let request = procedure.ag_update(AgUpdate::Ok, &mut slc_state);
        assert_matches!(request, ProcedureRequest::None);

        assert_matches!(procedure, CodecConnectionSetupProcedure::SynchronousConnectionSetup);
    }

    fn expect_codec_negotiation_codec(supported: Vec<CodecId>, expected: CodecId) {
        let mut slc_state = SlcState {
            ag_features: AgFeatures::CODEC_NEGOTIATION,
            hf_features: HfFeatures::CODEC_NEGOTIATION,
            hf_supported_codecs: Some(supported.clone()),
            ..SlcState::default()
        };

        let mut procedure = CodecConnectionSetupProcedure::new();
        let request = procedure.ag_update(AgUpdate::CodecSetup(None), &mut slc_state);
        let expected_messages = vec![at::success(at::Success::Bcs { codec: expected.into() })];
        assert_matches!(request, ProcedureRequest::SendMessages(m) if m == expected_messages);
    }

    #[test]
    fn codec_negotiation_chooses_best_supported() {
        // TODO(fxbug.dev/81374): This should request MSBC if it's supported
        expect_codec_negotiation_codec(vec![CodecId::MSBC, CodecId::CVSD], CodecId::CVSD);
        expect_codec_negotiation_codec(vec![CodecId::CVSD, 0xf0.into()], CodecId::CVSD);
    }

    #[test]
    fn unexpected_hf_update_returns_error() {
        let mut procedure = CodecConnectionSetupProcedure::new();
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
        let mut procedure = CodecConnectionSetupProcedure::new();
        let mut state = SlcState::default();
        // SLCI AT command.
        let random_ag = AgUpdate::ThreeWaySupport;
        assert_matches!(
            procedure.ag_update(random_ag, &mut state),
            ProcedureRequest::Error(ProcedureError::UnexpectedAg(_))
        );
    }

    #[test]
    fn peer_inititated_startup_and_selection() {
        let both_codecs = vec![CodecId::MSBC, CodecId::CVSD];
        let mut slc_state = SlcState {
            ag_features: AgFeatures::CODEC_NEGOTIATION,
            hf_features: HfFeatures::CODEC_NEGOTIATION,
            hf_supported_codecs: Some(both_codecs.clone()),
            ..SlcState::default()
        };
        let mut procedure = CodecConnectionSetupProcedure::new();

        let request = procedure.hf_update(at::Command::Bcc {}, &mut slc_state);
        // TODO(fxbug.dev/81374): This should request MSBC if it's supported
        let expected_messages =
            vec![at::Response::Ok, at::success(at::Success::Bcs { codec: CodecId::CVSD.into() })];
        assert_matches!(request, ProcedureRequest::SendMessages(m) if m == expected_messages);

        let request =
            procedure.hf_update(at::Command::Bcs { codec: CodecId::CVSD.into() }, &mut slc_state);
        assert_matches!(
            request,
            ProcedureRequest::Request(SlcRequest::SynchronousConnectionSetup { .. })
        );

        // We require two Oks, of which the first one is just an OK response to the codec request,
        // and the second represents the result of setting up the SCO and Audio.
        let request = procedure.ag_update(AgUpdate::Ok, &mut slc_state);
        let expected_messages = vec![at::Response::Ok];
        assert_matches!(request, ProcedureRequest::SendMessages(m) if m == expected_messages);
        let request = procedure.ag_update(AgUpdate::Ok, &mut slc_state);
        assert_matches!(request, ProcedureRequest::None);
    }
}
