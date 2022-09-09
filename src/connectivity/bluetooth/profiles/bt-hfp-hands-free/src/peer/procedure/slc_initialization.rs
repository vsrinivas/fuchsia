// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use at_commands as at;

use super::{Procedure, ProcedureMarker};

use crate::features::{AgFeatures, CVSD, MSBC};
use crate::peer::service_level_connection::SharedState;

/// This mode's behavior is to forward unsolicited result codes directly per
/// 3GPP TS 27.007 version 6.8.0, Section 8.10.
/// This is the only supported mode in the Event Reporting Enabling AT command (AT+CMER).
/// Defined in HFP v1.8 Section 4.34.2.
pub const INDICATOR_REPORTING_MODE: i64 = 3;

#[derive(Debug)]
pub enum Stages {
    Features,
    CodecNegotiation,
    ListIndicators,
    EnableIndicators,
    IndicatorStatusUpdate,
}

// TODO(fxbug.dev/104703): Still work in progress
pub struct SlcInitProcedure {
    terminated: bool,
    state: Stages,
}

impl SlcInitProcedure {
    pub fn new() -> Self {
        Self { terminated: false, state: Stages::Features }
    }

    #[cfg(test)]
    pub fn start_at_state(state: Stages) -> Self {
        Self { state, ..SlcInitProcedure::new() }
    }

    #[cfg(test)]
    pub fn start_terminated() -> Self {
        Self { terminated: true, ..SlcInitProcedure::new() }
    }
}

impl Procedure for SlcInitProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::SlcInitialization
    }

    /// Checks for sequential ordering of commands by first checking the
    /// stage the SLCI is in and then extract important data from AG responses
    /// and proceed to next stage if necessary.
    fn ag_update(
        &mut self,
        state: &mut SharedState,
        update: &Vec<at::Response>,
    ) -> Result<Vec<at::Command>, Error> {
        if self.is_terminated() {
            return Err(format_err!("Procedure is already terminated at {:?} stage before processing update for response(s): {:?}.", self.state, update));
        }
        match self.state {
            Stages::Features => {
                match update[..] {
                    [at::Response::Success(at::Success::Brsf { features }), at::Response::Ok] => {
                        state.ag_features = AgFeatures::from_bits_truncate(features);
                        if state.supports_codec_negotiation() {
                            self.state = Stages::CodecNegotiation;
                            // By default, we support the CVSD and MSBC codecs.
                            return Ok(vec![at::Command::Bac {
                                codecs: vec![CVSD.into(), MSBC.into()],
                            }]);
                        } else {
                            self.state = Stages::ListIndicators;
                            return Ok(vec![at::Command::CindTest {}]);
                        }
                    }
                    _ => {
                        return Err(format_err!(
                            "Wrong responses at {:?} stage of SLCI with response(s): {:?}.",
                            self.state,
                            update
                        ));
                    }
                }
            }
            Stages::CodecNegotiation => match update[..] {
                [at::Response::Ok] => {
                    self.state = Stages::ListIndicators;
                    return Ok(vec![at::Command::CindTest {}]);
                }
                _ => {
                    return Err(format_err!(
                        "Wrong responses at {:?} stage of SLCI with response(s): {:?}.",
                        self.state,
                        update
                    ));
                }
            },
            Stages::ListIndicators => {
                match update[..] {
                    // TODO(fxbug.dev/108331): Read additional indicators by parsing raw bytes instead of just checking for existence of raw bytes.
                    [at::Response::RawBytes(_), at::Response::Ok] => {
                        self.state = Stages::EnableIndicators;
                        return Ok(vec![at::Command::CindRead {}]);
                    }
                    _ => {
                        return Err(format_err!(
                            "Wrong responses at {:?} stage of SLCI with response(s): {:?}.",
                            self.state,
                            update
                        ));
                    }
                }
            }
            Stages::EnableIndicators => match &update[..] {
                [at::Response::Success(cmd @ at::Success::Cind { .. }), at::Response::Ok] => {
                    state.ag_indicators.update_indicator_values(&cmd);
                    self.state = Stages::IndicatorStatusUpdate;
                    return Ok(vec![at::Command::Cmer {
                        mode: INDICATOR_REPORTING_MODE,
                        keyp: 0,
                        disp: 0,
                        ind: 1,
                    }]);
                }
                _ => {
                    return Err(format_err!(
                        "Wrong responses at {:?} stage of SLCI with response(s): {:?}.",
                        self.state,
                        update
                    ));
                }
            },
            Stages::IndicatorStatusUpdate => {
                match update[..] {
                    [at::Response::Ok] => {
                        // TODO (fxbug.dev/108596): Implement Call hold and multiparty services in addiotn to Generic Status Indicators.
                        if state.supports_three_way_calling() {
                            return Ok(vec![at::Command::Chld { command: String::from("") }]);
                        } else {
                            self.terminated = true;
                            return Ok(vec![]);
                        }
                    }
                    _ => {
                        return Err(format_err!(
                            "Wrong responses at {:?} stage of SLCI with response(s): {:?}.",
                            self.state,
                            update
                        ));
                    }
                }
            }
        }
    }

    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::config::HandsFreeFeatureSupport;
    use assert_matches::assert_matches;

    // TODO(fxb/71668) Stop using raw bytes.
    const CIND_TEST_RESPONSE_BYTES: &[u8] = b"+CIND: \
    (\"service\",(0,1)),\
    (\"call\",(0,1)),\
    (\"callsetup\",(0,3)),\
    (\"callheld\",(0,2)),\
    (\"signal\",(0,5)),\
    (\"roam\",(0,1)),\
    (\"battchg\",(0,5)\
    )";

    #[fuchsia::test]
    /// Checks that the mandatory exchanges between the AG and HF roles properly progresses
    /// our state and sends the expected responses until our procedure it marked complete.
    fn slci_mandatory_exchanges_and_termination() {
        let mut procedure = SlcInitProcedure::new();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        let response1 = vec![
            at::Response::Success(at::Success::Brsf { features: AgFeatures::default().bits() }),
            at::Response::Ok,
        ];
        let expected_command1 = vec![at::Command::CindTest {}];
        assert_eq!(procedure.ag_update(&mut state, &response1).unwrap(), expected_command1);

        let indicator_msg = CIND_TEST_RESPONSE_BYTES.to_vec();
        let response2 = vec![at::Response::RawBytes(indicator_msg), at::Response::Ok];
        let expected_command2 = vec![at::Command::CindRead {}];
        assert_eq!(procedure.ag_update(&mut state, &response2).unwrap(), expected_command2);

        let response3 = vec![
            at::Response::Success(at::Success::Cind {
                service: false,
                call: false,
                callsetup: 0,
                callheld: 0,
                signal: 0,
                roam: false,
                battchg: 0,
            }),
            at::Response::Ok,
        ];
        let update3 =
            vec![at::Command::Cmer { mode: INDICATOR_REPORTING_MODE, keyp: 0, disp: 0, ind: 1 }];
        assert_eq!(procedure.ag_update(&mut state, &response3).unwrap(), update3);

        let response4 = vec![at::Response::Ok];
        let update4 = Vec::<at::Command>::new();
        assert_eq!(procedure.ag_update(&mut state, &response4).unwrap(), update4);

        assert!(procedure.is_terminated());
    }

    #[fuchsia::test]
    fn slci_codec_negotiation_properly_works() {
        let mut procedure = SlcInitProcedure::new();
        //wide band needed for codec negotiation.
        let config = HandsFreeFeatureSupport {
            wide_band_speech: true,
            ..HandsFreeFeatureSupport::default()
        };
        let mut ag_features = AgFeatures::default();
        ag_features.set(AgFeatures::CODEC_NEGOTIATION, true);
        let mut state = SharedState::load_with_set_ag_features(config, ag_features);

        assert!(!procedure.is_terminated());

        let response1 = vec![
            at::Response::Success(at::Success::Brsf { features: ag_features.bits() }),
            at::Response::Ok,
        ];
        let expected_command1 = vec![at::Command::Bac { codecs: vec![CVSD.into(), MSBC.into()] }];

        assert_eq!(procedure.ag_update(&mut state, &response1).unwrap(), expected_command1);
        assert_matches!(procedure.state, Stages::CodecNegotiation);

        let response2 = vec![at::Response::Ok];
        let expected_command2 = vec![at::Command::CindTest {}];

        assert_eq!(procedure.ag_update(&mut state, &response2).unwrap(), expected_command2);
        assert_matches!(procedure.state, Stages::ListIndicators);
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_feature_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::Features);
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        // Missing the accompanying Ok response so should result in error.
        let wrong_response = vec![at::Response::Success(at::Success::Brsf { features: 0 })];
        assert_matches!(procedure.ag_update(&mut state, &wrong_response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_codec_negotiation_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::CodecNegotiation);
        let config = HandsFreeFeatureSupport {
            wide_band_speech: true,
            ..HandsFreeFeatureSupport::default()
        };
        let mut ag_features = AgFeatures::default();
        ag_features.set(AgFeatures::CODEC_NEGOTIATION, true);
        let mut state = SharedState::load_with_set_ag_features(config, ag_features);

        assert!(!procedure.is_terminated());

        // Did not receive expected Ok response as should result in error..
        let wrong_response = vec![at::Response::Success(at::Success::TestResponse {})];
        assert_matches!(procedure.ag_update(&mut state, &wrong_response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_list_indicators_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::ListIndicators);
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        // Missing the accompanying Ok response so should result in error.
        let wrong_response = vec![at::Response::RawBytes(CIND_TEST_RESPONSE_BYTES.to_vec())];
        assert_matches!(procedure.ag_update(&mut state, &wrong_response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_enable_indicators_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::EnableIndicators);
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        // Missing the accompanying Ok response so should result in error.
        let wrong_response = vec![at::Response::Success(at::Success::Cind {
            service: false,
            call: false,
            callsetup: 0,
            callheld: 0,
            signal: 0,
            roam: false,
            battchg: 0,
        })];
        assert_matches!(procedure.ag_update(&mut state, &wrong_response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_indicator_update_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::IndicatorStatusUpdate);
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        // Did not receive expected Ok response as should result in error.
        let wrong_response = vec![at::Response::Success(at::Success::TestResponse {})];
        assert_matches!(procedure.ag_update(&mut state, &wrong_response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_update_on_terminated_procedure() {
        let mut procedure = SlcInitProcedure::start_terminated();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(procedure.is_terminated());
        // Valid response of first step of SLCI
        let valid_response = vec![
            at::Response::Success(at::Success::Brsf { features: AgFeatures::default().bits() }),
            at::Response::Ok,
        ];
        let update = procedure.ag_update(&mut state, &valid_response);
        assert_matches!(update, Err(_));
    }
}
