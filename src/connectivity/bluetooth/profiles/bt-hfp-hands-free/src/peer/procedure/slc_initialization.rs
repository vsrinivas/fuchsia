// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use at_commands as at;

use super::{Procedure, ProcedureMarker};

use crate::features::{extract_features_from_command, AgFeatures, CVSD, MSBC};
use crate::peer::indicators::{BATTERY_LEVEL, ENHANCED_SAFETY, INDICATOR_REPORTING_MODE};
use crate::peer::service_level_connection::SharedState;

#[derive(Debug)]
pub enum Stages {
    Features,
    CodecNegotiation,
    ListIndicators,
    EnableIndicators,
    IndicatorStatusUpdate,
    CallHoldAndMultiParty,
    HfIndicator,
    HfIndicatorRequest,
    HfIndicatorEnable,
}

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
            Stages::IndicatorStatusUpdate => match update[..] {
                [at::Response::Ok] => {
                    if state.supports_three_way_calling() {
                        self.state = Stages::CallHoldAndMultiParty;
                        return Ok(vec![at::Command::ChldTest {}]);
                    } else if state.supports_hf_indicators() {
                        self.state = Stages::HfIndicator;
                        return Ok(vec![at::Command::Bind {
                            indicators: vec![ENHANCED_SAFETY as i64, BATTERY_LEVEL as i64],
                        }]);
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
            },
            Stages::CallHoldAndMultiParty => match &update[..] {
                [at::Response::Success(at::Success::Chld { commands }), at::Response::Ok] => {
                    state.three_way_features = extract_features_from_command(&commands)?;
                    if state.supports_hf_indicators() {
                        self.state = Stages::HfIndicator;
                        return Ok(vec![at::Command::Bind {
                            indicators: vec![ENHANCED_SAFETY as i64, BATTERY_LEVEL as i64],
                        }]);
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
            },
            Stages::HfIndicator => match update[..] {
                [at::Response::Ok] => {
                    self.state = Stages::HfIndicatorRequest;
                    return Ok(vec![at::Command::BindTest {}]);
                }
                _ => {
                    return Err(format_err!(
                        "Wrong responses at {:?} stage of SLCI with response(s): {:?}.",
                        self.state,
                        update
                    ));
                }
            },
            Stages::HfIndicatorRequest => match &update[..] {
                [at::Response::Success(at::Success::BindList { indicators }), at::Response::Ok] => {
                    state.hf_indicators.set_supported_indicators(indicators);
                    self.state = Stages::HfIndicatorEnable;
                    return Ok(vec![at::Command::BindRead {}]);
                }
                _ => {
                    return Err(format_err!(
                        "Wrong responses at {:?} stage of SLCI with response(s): {:?}.",
                        self.state,
                        update
                    ));
                }
            },
            Stages::HfIndicatorEnable => {
                for response in update {
                    match response {
                        at::Response::Success(cmd @ at::Success::BindStatus { .. }) => {
                            state.hf_indicators.change_indicator_state(cmd)?;
                        }
                        at::Response::Ok => {
                            self.terminated = true;
                            return Ok(vec![]);
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
                return Err(format_err!(
                    "Did not receive Ok from AG at {:?} stage of SLCI with response(s): {:?}.",
                    self.state,
                    update
                ));
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

    use crate::{config::HandsFreeFeatureSupport, features::CallHoldAction, features::HfFeatures};
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
    fn slci_hf_indicator_properly_works() {
        let mut procedure = SlcInitProcedure::new();
        // Hf indicators needed for optional procedure.
        let mut hf_features = HfFeatures::default();
        let mut ag_features = AgFeatures::default();
        hf_features.set(HfFeatures::HF_INDICATORS, true);
        ag_features.set(AgFeatures::HF_INDICATORS, true);
        let mut state = SharedState::load_with_set_features(hf_features, ag_features);

        assert!(!state.hf_indicators.enhanced_safety.1);
        assert!(!state.hf_indicators.battery_level.1);
        assert!(!state.hf_indicators.enhanced_safety.0.enabled);
        assert!(!state.hf_indicators.battery_level.0.enabled);
        assert!(!procedure.is_terminated());

        let response1 = vec![
            at::Response::Success(at::Success::Brsf { features: ag_features.bits() }),
            at::Response::Ok,
        ];
        let expected_command1 = vec![at::Command::CindTest {}];

        assert_eq!(procedure.ag_update(&mut state, &response1).unwrap(), expected_command1);
        assert_matches!(procedure.state, Stages::ListIndicators);

        let indicator_msg = CIND_TEST_RESPONSE_BYTES.to_vec();
        let response2 = vec![at::Response::RawBytes(indicator_msg), at::Response::Ok];
        let expected_command2 = vec![at::Command::CindRead {}];

        assert_eq!(procedure.ag_update(&mut state, &response2).unwrap(), expected_command2);
        assert_matches!(procedure.state, Stages::EnableIndicators);

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
        let expected_command3 =
            vec![at::Command::Cmer { mode: INDICATOR_REPORTING_MODE, keyp: 0, disp: 0, ind: 1 }];

        assert_eq!(procedure.ag_update(&mut state, &response3).unwrap(), expected_command3);
        assert_matches!(procedure.state, Stages::IndicatorStatusUpdate);

        let response4 = vec![at::Response::Ok];
        let expected_command4 = vec![at::Command::Bind {
            indicators: vec![ENHANCED_SAFETY as i64, BATTERY_LEVEL as i64],
        }];
        assert_eq!(procedure.ag_update(&mut state, &response4).unwrap(), expected_command4);
        assert_matches!(procedure.state, Stages::HfIndicator);

        let response5 = vec![at::Response::Ok];
        let expected_command5 = vec![at::Command::BindTest {}];
        assert_eq!(procedure.ag_update(&mut state, &response5).unwrap(), expected_command5);
        assert_matches!(procedure.state, Stages::HfIndicatorRequest);

        let response6 = vec![
            at::Response::Success(at::Success::BindList {
                indicators: vec![
                    at::BluetoothHFIndicator::BatteryLevel,
                    at::BluetoothHFIndicator::EnhancedSafety,
                ],
            }),
            at::Response::Ok,
        ];
        let expected_command6 = vec![at::Command::BindRead {}];
        assert_eq!(procedure.ag_update(&mut state, &response6).unwrap(), expected_command6);
        assert!(state.hf_indicators.enhanced_safety.1);
        assert!(state.hf_indicators.battery_level.1);
        assert_matches!(procedure.state, Stages::HfIndicatorEnable);

        let response7 = vec![
            at::Response::Success(at::Success::BindStatus {
                anum: at::BluetoothHFIndicator::EnhancedSafety,
                state: true,
            }),
            at::Response::Success(at::Success::BindStatus {
                anum: at::BluetoothHFIndicator::BatteryLevel,
                state: true,
            }),
            at::Response::Ok,
        ];
        let expected_command7 = Vec::<at::Command>::new();
        assert_eq!(procedure.ag_update(&mut state, &response7).unwrap(), expected_command7);
        assert!(state.hf_indicators.enhanced_safety.0.enabled);
        assert!(state.hf_indicators.battery_level.0.enabled);
        assert!(procedure.is_terminated());
    }

    #[fuchsia::test]
    fn slci_codec_negotiation_properly_works() {
        let mut procedure = SlcInitProcedure::new();
        let mut hf_features = HfFeatures::default();
        hf_features.set(HfFeatures::CODEC_NEGOTIATION, true);
        let mut ag_features = AgFeatures::default();
        ag_features.set(AgFeatures::CODEC_NEGOTIATION, true);
        let mut state = SharedState::load_with_set_features(hf_features, ag_features);

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
    fn slci_three_way_feature_proper_works() {
        let mut procedure = SlcInitProcedure::new();
        // Three way calling needed for stage progression.
        let mut hf_features = HfFeatures::default();
        hf_features.set(HfFeatures::THREE_WAY_CALLING, true);
        let mut ag_features = AgFeatures::default();
        ag_features.set(AgFeatures::THREE_WAY_CALLING, true);
        let mut state = SharedState::load_with_set_features(hf_features, ag_features);

        assert!(!procedure.is_terminated());

        let response1 = vec![
            at::Response::Success(at::Success::Brsf { features: ag_features.bits() }),
            at::Response::Ok,
        ];
        let expected_command1 = vec![at::Command::CindTest {}];

        assert_eq!(procedure.ag_update(&mut state, &response1).unwrap(), expected_command1);
        assert_matches!(procedure.state, Stages::ListIndicators);

        let indicator_msg = CIND_TEST_RESPONSE_BYTES.to_vec();
        let response2 = vec![at::Response::RawBytes(indicator_msg), at::Response::Ok];
        let expected_command2 = vec![at::Command::CindRead {}];
        assert_eq!(procedure.ag_update(&mut state, &response2).unwrap(), expected_command2);
        assert_matches!(procedure.state, Stages::EnableIndicators);

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
        assert_matches!(procedure.state, Stages::IndicatorStatusUpdate);

        let response4 = vec![at::Response::Ok];
        let update4 = vec![at::Command::ChldTest {}];
        assert_eq!(procedure.ag_update(&mut state, &response4).unwrap(), update4);
        assert_matches!(procedure.state, Stages::CallHoldAndMultiParty);

        let commands = vec![
            String::from("0"),
            String::from("1"),
            String::from("2"),
            String::from("11"),
            String::from("22"),
            String::from("3"),
            String::from("4"),
        ];
        let response5 =
            vec![at::Response::Success(at::Success::Chld { commands }), at::Response::Ok];
        let update5 = Vec::<at::Command>::new();
        assert_eq!(procedure.ag_update(&mut state, &response5).unwrap(), update5);
        assert!(procedure.is_terminated());

        let features = vec![
            CallHoldAction::ReleaseAllHeld,
            CallHoldAction::ReleaseAllActive,
            CallHoldAction::HoldActiveAndAccept,
            CallHoldAction::ReleaseSpecified(1),
            CallHoldAction::HoldAllExceptSpecified(2),
            CallHoldAction::AddCallToHeldConversation,
            CallHoldAction::ExplicitCallTransfer,
        ];

        assert_eq!(features, state.three_way_features);
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
        let mut hf_features = HfFeatures::default();
        hf_features.set(HfFeatures::CODEC_NEGOTIATION, true);
        let mut ag_features = AgFeatures::default();
        ag_features.set(AgFeatures::CODEC_NEGOTIATION, true);
        let mut state = SharedState::load_with_set_features(hf_features, ag_features);

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
    fn error_when_incorrect_response_at_call_hold_stage_non_number_index() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::CallHoldAndMultiParty);
        let config = HandsFreeFeatureSupport {
            call_waiting_or_three_way_calling: true,
            ..HandsFreeFeatureSupport::default()
        };
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        let invalid_command = vec![String::from("1A")];
        let response = vec![
            at::Response::Success(at::Success::Chld { commands: invalid_command }),
            at::Response::Ok,
        ];

        assert_matches!(procedure.ag_update(&mut state, &response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_call_hold_stage_invalid_command() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::CallHoldAndMultiParty);
        let config = HandsFreeFeatureSupport {
            call_waiting_or_three_way_calling: true,
            ..HandsFreeFeatureSupport::default()
        };
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        let invalid_command = vec![String::from("5")];
        let response = vec![
            at::Response::Success(at::Success::Chld { commands: invalid_command }),
            at::Response::Ok,
        ];

        assert_matches!(procedure.ag_update(&mut state, &response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_hf_indicator_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::HfIndicator);
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        // Did not receive expected Ok response as should result in error.
        let wrong_response = vec![at::Response::Success(at::Success::TestResponse {})];
        assert_matches!(procedure.ag_update(&mut state, &wrong_response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_hf_indicator_request_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::HfIndicatorRequest);
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        // Did not receive expected Ok response as should result in error.
        let wrong_response = vec![at::Response::Success(at::Success::TestResponse {})];
        assert_matches!(procedure.ag_update(&mut state, &wrong_response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_incorrect_response_at_hf_indicator_enable_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::HfIndicatorEnable);
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        // Did not receive expected Ok response as should result in error.
        let wrong_response = vec![at::Response::Success(at::Success::TestResponse {})];
        assert_matches!(procedure.ag_update(&mut state, &wrong_response), Err(_));
        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn error_when_no_ok_at_hf_indicator_enable_stage() {
        let mut procedure = SlcInitProcedure::start_at_state(Stages::HfIndicatorEnable);
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        assert!(!procedure.is_terminated());

        // Did not receive expected Ok response as should result in error.
        let wrong_response = vec![at::Response::Success(at::Success::BindStatus {
            anum: at::BluetoothHFIndicator::BatteryLevel,
            state: true,
        })];
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
