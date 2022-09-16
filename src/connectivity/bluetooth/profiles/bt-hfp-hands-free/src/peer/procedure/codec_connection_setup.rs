// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use at_commands as at;
use tracing::warn;

use super::{Procedure, ProcedureMarker};

use crate::peer::service_level_connection::SharedState;

pub struct CodecConnectionSetupProcedure {
    // Whether the procedure has sent the phone status to the HF.
    terminated: bool,
}

impl CodecConnectionSetupProcedure {
    pub fn new() -> Self {
        Self { terminated: false }
    }
}

impl Procedure for CodecConnectionSetupProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::CodecConnectionSetup
    }

    fn ag_update(
        &mut self,
        state: &mut SharedState,
        update: &Vec<at::Response>,
    ) -> Result<Vec<at::Command>, Error> {
        // TODO(fxbug.dev/109735) - Put in stages like SLCI procedure.
        if !self.is_terminated() {
            match update[..] {
                [at::Response::Success(at::Success::Bcs { codec })] => {
                    if state.supported_codecs.contains(&(codec as u8)) {
                        state.selected_codec = Some(codec as u8);
                        Ok(vec![at::Command::Bcs { codec: codec }])
                    } else {
                        // According to HFP v1.8 Section 4.11.3, if the received codec ID is
                        // not available, the HF shalle respond with AT+BAC with its available
                        // codecs.
                        warn!("Codec received is not supported. Sending supported codecs to AG.");
                        self.terminated = true;
                        let supported_codecs =
                            state.supported_codecs.iter().map(|&x| x as i64).collect();
                        Ok(vec![at::Command::Bac { codecs: supported_codecs }])
                    }
                }
                [at::Response::Ok] => {
                    self.terminated = true;
                    Ok(vec![])
                }
                _ => {
                    return Err(format_err!(
                        "Received invalid response during a codec connection setup procedure: {:?}",
                        update
                    ));
                }
            }
        } else {
            return Err(format_err!(
                "Received response while terminated during a codec connection setup procedure: {:?}",
                update
            ));
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

    use crate::config::HandsFreeFeatureSupport;
    use crate::features::{CVSD, MSBC};

    #[fuchsia::test]
    fn properly_responds_to_supported_codec() {
        let mut procedure = CodecConnectionSetupProcedure::new();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);
        let agreed_codec = CVSD;

        let response1 =
            vec![at::Response::Success(at::Success::Bcs { codec: agreed_codec as i64 })];

        assert!(!procedure.is_terminated());

        assert_matches!(procedure.ag_update(&mut state, &response1), Ok(_));
        assert_eq!(state.selected_codec.expect("Codec agreed upon."), agreed_codec);

        let response2 = vec![at::Response::Ok];
        assert_matches!(procedure.ag_update(&mut state, &response2), Ok(_));

        assert!(procedure.is_terminated())
    }

    #[fuchsia::test]
    fn properly_responds_to_unsupported_codec() {
        let mut procedure = CodecConnectionSetupProcedure::new();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);
        let unsupported_codec = MSBC;

        let response =
            vec![at::Response::Success(at::Success::Bcs { codec: unsupported_codec as i64 })];

        assert!(!procedure.is_terminated());

        assert_matches!(procedure.ag_update(&mut state, &response), Ok(_));
        assert_matches!(state.selected_codec, None);

        assert!(procedure.is_terminated())
    }

    #[fuchsia::test]
    fn error_from_invalid_responses() {
        let mut procedure = CodecConnectionSetupProcedure::new();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        let response = vec![at::Response::Success(at::Success::TestResponse {})];

        assert!(!procedure.is_terminated());

        assert_matches!(procedure.ag_update(&mut state, &response), Err(_));

        assert!(!procedure.is_terminated())
    }
}
