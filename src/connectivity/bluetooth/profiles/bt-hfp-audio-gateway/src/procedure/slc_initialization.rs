// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    AgUpdate, InformationRequest, Procedure, ProcedureError as Error, ProcedureMarker,
    ProcedureRequest,
};

use crate::{
    indicator_status::IndicatorStatus,
    peer::service_level_connection::SlcState,
    protocol::features::{AgFeatures, HfFeatures},
};

use at_commands as at;

/// This mode's behavior is to forward unsolicited result codes directly per
/// 3GPP TS 27.007 version 6.8.0, Section 8.10.
/// This is the only supported mode in the Event Reporting Enabling AT command (AT+CMER).
/// Defined in HFP v1.8 Section 4.34.2.
const SUPPORTED_EVENT_REPORTING_MODE: i64 = 3;

/// A singular state within the SLC Initialization Procedure.
pub trait SlcProcedureState {
    /// Returns the next state in the procedure based on the current state and the given
    /// AG `update`.
    /// By default, the state transition will return an error. Implementors should only
    /// implement this for valid transitions.
    fn ag_update(&self, update: AgUpdate, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        SlcErrorState::unexpected_ag(update)
    }

    /// Returns the next state in the procedure based on the current state and the given
    /// HF `update`.
    /// By default, the state transition will return an error. Implementors should only
    /// implement this for valid transitions.
    fn hf_update(&self, update: at::Command, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        SlcErrorState::unexpected_hf(update)
    }

    /// Returns the request for this given state.
    fn request(&self) -> ProcedureRequest;

    /// Returns true if this is the final state in the Procedure.
    fn is_terminal(&self) -> bool {
        false
    }
}

/// Represents the current state of the Service Level Connection initialization procedure
/// as defined in HFP v1.8 Section 4.2. Provides an interface for driving the procedure
/// given inputs from the AG and HF.
///
/// This procedure is implemented from the perspective of the AG. Namely, outgoing `requests`
/// typically request information about the current state of the AG, to be sent to the remote
/// peer acting as the HF.
/// The state machine of this procedure looks like:
///   1) SlcInitStart
///   2) HfFeatures
///   3) AgFeatures
///   4) (optional) Codecs
///   5) AgSupportedIndicators
///   6) AgIndicatorStatusRequest
///   7) AgIndicatorStatus
///   8) AgIndicatorStatusEnable
///   9) (optional) 3-way support
///   10) (optional) HfSupportedIndicators
///   11) (optional) ListSupportedGenericIndicators
pub struct SlcInitProcedure {
    state: Box<dyn SlcProcedureState>,
}

impl SlcInitProcedure {
    pub fn new() -> Self {
        Self { state: Box::new(SlcInitStart::default()) }
    }

    /// Builds the SLC Initialization Procedure starting at the given `state`.
    #[cfg(test)]
    pub fn new_at_state(state: impl SlcProcedureState + 'static) -> Self {
        Self { state: Box::new(state) }
    }
}

impl Procedure for SlcInitProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::SlcInitialization
    }

    fn hf_update(&mut self, update: at::Command, state: &mut SlcState) -> ProcedureRequest {
        if self.is_terminated() {
            return ProcedureRequest::Error(Error::AlreadyTerminated);
        }

        self.state = self.state.hf_update(update, state);
        self.state.request()
    }

    fn ag_update(&mut self, update: AgUpdate, state: &mut SlcState) -> ProcedureRequest {
        if self.is_terminated() {
            return ProcedureRequest::Error(Error::AlreadyTerminated);
        }

        self.state = self.state.ag_update(update, state);
        self.state.request()
    }

    fn is_terminated(&self) -> bool {
        self.state.is_terminal()
    }
}

/// This is the default starting point of the Service Level Connection Initialization procedure.
/// Per HFP v1.8 Section 4.2.1.6, the HF always initiates this procedure.
#[derive(Debug, Default, Clone)]
struct SlcInitStart;

impl SlcProcedureState for SlcInitStart {
    fn request(&self) -> ProcedureRequest {
        ProcedureRequest::None
    }

    fn hf_update(&self, update: at::Command, state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        // Only the HF request containing its features can continue the SLC initialization process.
        match update {
            at::Command::Brsf { features } => {
                state.hf_features = HfFeatures::from_bits_truncate(features as u32);
                Box::new(HfFeaturesReceived)
            }
            m => SlcErrorState::unexpected_hf(m),
        }
    }
}

/// We've received a supported features request from the HF.
#[derive(Debug, Clone)]
struct HfFeaturesReceived;

impl SlcProcedureState for HfFeaturesReceived {
    fn request(&self) -> ProcedureRequest {
        InformationRequest::GetAgFeatures {
            response: Box::new(|features: AgFeatures| AgUpdate::Features(features)),
        }
        .into()
    }

    fn ag_update(&self, update: AgUpdate, state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        // Only the AG request containing its features can continue the process.
        match update {
            AgUpdate::Features(features) => {
                state.ag_features = features;

                Box::new(AgFeaturesReceived { state: state.clone() })
            }
            m => SlcErrorState::unexpected_ag(m),
        }
    }
}

#[derive(Debug, Clone)]
struct AgFeaturesReceived {
    state: SlcState,
}

impl SlcProcedureState for AgFeaturesReceived {
    fn request(&self) -> ProcedureRequest {
        AgUpdate::Features(self.state.ag_features).into()
    }

    fn hf_update(&self, update: at::Command, state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        // The codec negotiation step of this procedure is optional and is determined by the
        // availability specified in the feature flags of the HF and AG.
        match (update, state.codec_negotiation()) {
            (at::Command::Bac { codecs }, true) => {
                state.hf_supported_codecs = Some(codecs.into_iter().map(|x| x as u32).collect());
                Box::new(AvailableCodecsReceived)
            }
            (at::Command::CindTest {}, false) => Box::new(AgSupportedIndicatorsRequested),
            (m, _) => SlcErrorState::unexpected_hf(m),
        }
    }
}

/// We've received the available codecs from the HF.
#[derive(Debug, Clone)]
struct AvailableCodecsReceived;

impl SlcProcedureState for AvailableCodecsReceived {
    fn request(&self) -> ProcedureRequest {
        AgUpdate::Ok.into()
    }

    fn hf_update(&self, update: at::Command, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        // Only a HF request for AG indicators can continue the procedure.
        match update {
            at::Command::CindTest {} => Box::new(AgSupportedIndicatorsRequested),
            m => SlcErrorState::unexpected_hf(m),
        }
    }
}

#[derive(Debug, Clone)]
struct AgSupportedIndicatorsRequested;

impl SlcProcedureState for AgSupportedIndicatorsRequested {
    fn request(&self) -> ProcedureRequest {
        AgUpdate::SupportedIndicators.into()
    }

    fn hf_update(&self, update: at::Command, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        // Only a HF request for the current status of the AG indicators will
        // continue the procedure.
        match update {
            at::Command::CindRead {} => Box::new(AgIndicatorStatusRequestReceived),
            m => SlcErrorState::unexpected_hf(m),
        }
    }
}

#[derive(Debug, Clone)]
struct AgIndicatorStatusRequestReceived;

impl SlcProcedureState for AgIndicatorStatusRequestReceived {
    fn request(&self) -> ProcedureRequest {
        InformationRequest::GetAgIndicatorStatus {
            response: Box::new(|status: IndicatorStatus| AgUpdate::IndicatorStatus(status)),
        }
        .into()
    }

    fn ag_update(&self, update: AgUpdate, state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        // Only the current status information from the AG will continue the procedure.
        match update {
            AgUpdate::IndicatorStatus(status) => {
                state.ag_indicator_status = status;
                Box::new(AgIndicatorStatusReceived { state: state.clone() })
            }
            m => SlcErrorState::unexpected_ag(m),
        }
    }
}

#[derive(Debug, Clone)]
struct AgIndicatorStatusReceived {
    state: SlcState,
}

impl SlcProcedureState for AgIndicatorStatusReceived {
    fn request(&self) -> ProcedureRequest {
        AgUpdate::IndicatorStatus(self.state.ag_indicator_status).into()
    }

    fn hf_update(&self, update: at::Command, state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        // Only a HF request to enable the AG Indicator Status will continue the procedure.
        // Ensure that the requested `mode` and `ind` values are valid per HFP v1.8 Section 4.34.2.
        match update {
            at::Command::Cmer { mode, ind, .. } => {
                let is_valid = mode == SUPPORTED_EVENT_REPORTING_MODE && (ind == 0 || ind == 1);
                if !is_valid {
                    return SlcErrorState::invalid_hf_argument(update.clone());
                }
                state.indicator_events_reporting = ind != 0;
                Box::new(AgIndicatorStatusEnableReceived { state: state.clone() })
            }
            m => SlcErrorState::unexpected_hf(m),
        }
    }
}

/// The last mandatory step in the procedure. After this, there are optional
/// things that can be received.
#[derive(Debug, Clone)]
struct AgIndicatorStatusEnableReceived {
    state: SlcState,
}

impl SlcProcedureState for AgIndicatorStatusEnableReceived {
    fn request(&self) -> ProcedureRequest {
        AgUpdate::Ok.into()
    }

    fn hf_update(&self, update: at::Command, state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        if self.is_terminal() {
            return SlcErrorState::already_terminated();
        }

        // If both parties support three way calling, then we expect the 3-way support message
        // from the HF.
        if state.three_way_calling() {
            return match update {
                at::Command::ChldTest {} => {
                    Box::new(ThreeWaySupportReceived { state: state.clone() })
                }
                m => SlcErrorState::unexpected_hf(m),
            };
        }

        // Otherwise, both parties must be supporting HF Indicators (or else self.is_terminal()
        // would be true).
        match update {
            at::Command::CindRead {} => Box::new(HfSupportedIndicatorsReceived),
            m => SlcErrorState::unexpected_hf(m),
        }
    }

    fn is_terminal(&self) -> bool {
        // We don't continue if neither three way calling nor HF indicators are supported.
        !self.state.three_way_calling() && !self.state.hf_indicators()
    }
}

struct ThreeWaySupportReceived {
    state: SlcState,
}

impl SlcProcedureState for ThreeWaySupportReceived {
    fn request(&self) -> ProcedureRequest {
        AgUpdate::ThreeWaySupport.into()
    }

    fn hf_update(&self, update: at::Command, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        if self.is_terminal() {
            return SlcErrorState::already_terminated();
        }

        match update {
            at::Command::Bind { .. } => Box::new(HfSupportedIndicatorsReceived),
            m => SlcErrorState::unexpected_hf(m),
        }
    }

    fn is_terminal(&self) -> bool {
        // This is the final state if one or both parties don't support the HF Indicators.
        !self.state.hf_indicators()
    }
}

struct HfSupportedIndicatorsReceived;

impl SlcProcedureState for HfSupportedIndicatorsReceived {
    fn request(&self) -> ProcedureRequest {
        AgUpdate::Ok.into()
    }

    fn hf_update(&self, update: at::Command, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        match update {
            at::Command::BindTest {} => Box::new(AgSupportedIndicatorsReceived),
            m => SlcErrorState::unexpected_hf(m),
        }
    }
}

struct AgSupportedIndicatorsReceived;

impl SlcProcedureState for AgSupportedIndicatorsReceived {
    fn request(&self) -> ProcedureRequest {
        // TODO(fxb/71668) Stop using raw bytes.
        let bind_test_resp = format!(
            "+BIND:({},{})",
            at::BluetoothHFIndicator::BatteryLevel as i64,
            at::BluetoothHFIndicator::EnhancedSafety as i64
        )
        .into_bytes();
        vec![at::Response::RawBytes(bind_test_resp), at::Response::Ok].into()
    }

    fn hf_update(&self, update: at::Command, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        match update {
            at::Command::BindRead {} => Box::new(ListSupportedGenericIndicatorsReceived),
            m => SlcErrorState::unexpected_hf(m),
        }
    }
}

struct ListSupportedGenericIndicatorsReceived;

impl SlcProcedureState for ListSupportedGenericIndicatorsReceived {
    fn request(&self) -> ProcedureRequest {
        AgUpdate::SupportedHfIndicatorResponses { safety: true, battery: true }.into()
    }

    fn ag_update(&self, _update: AgUpdate, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        SlcErrorState::already_terminated()
    }

    fn hf_update(&self, _update: at::Command, _state: &mut SlcState) -> Box<dyn SlcProcedureState> {
        SlcErrorState::already_terminated()
    }

    fn is_terminal(&self) -> bool {
        // This is the last conditional state.
        true
    }
}

/// Represents the error state for this procedure. Any errors in the SLC
/// Initialization procedure will be considered fatal.
struct SlcErrorState {
    error: Error,
}

impl SlcErrorState {
    /// Builds and returns the error state for an unexpected AG message.
    fn unexpected_ag(m: AgUpdate) -> Box<dyn SlcProcedureState> {
        Box::new(SlcErrorState { error: Error::UnexpectedAg(m) })
    }

    /// Builds and returns the error state for an unexpected HF message.
    fn unexpected_hf(m: at::Command) -> Box<dyn SlcProcedureState> {
        Box::new(SlcErrorState { error: Error::UnexpectedHf(m) })
    }

    /// Builds and returns the error state for a HF message with invalid arguments.
    fn invalid_hf_argument(m: at::Command) -> Box<dyn SlcProcedureState> {
        Box::new(SlcErrorState { error: Error::InvalidHfArgument(m) })
    }

    /// Builds and returns the error state for the already terminated error case.
    fn already_terminated() -> Box<dyn SlcProcedureState> {
        Box::new(SlcErrorState { error: Error::AlreadyTerminated })
    }
}

impl SlcProcedureState for SlcErrorState {
    fn request(&self) -> ProcedureRequest {
        ProcedureRequest::Error(self.error.clone())
    }

    fn is_terminal(&self) -> bool {
        // The procedure should be considered terminated in the error state.
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::protocol::features::HfFeatures;
    use matches::assert_matches;

    #[test]
    fn supported_features_received_transition_to_codec_negotiation() {
        let mut state = SlcState {
            hf_features: HfFeatures::CODEC_NEGOTIATION,
            ag_features: AgFeatures::CODEC_NEGOTIATION,
            ..SlcState::default()
        };
        let mut procedure =
            SlcInitProcedure::new_at_state(AgFeaturesReceived { state: state.clone() });
        let update = at::Command::Bac { codecs: Vec::new() };
        // Both parties support codec negotiation, so upon receiving the Codec HF message, we
        // expect to successfully transition to the codec state and the resulting event
        // should be an Ack to the codecs.
        let event = procedure.hf_update(update, &mut state);
        assert_matches!(event, ProcedureRequest::SendMessages(msgs) if msgs == vec![at::Response::Ok]);
    }

    #[test]
    fn supported_features_received_transition_unexpected_update() {
        let mut state = SlcState {
            hf_features: HfFeatures::CODEC_NEGOTIATION,
            ag_features: AgFeatures::CODEC_NEGOTIATION,
            ..SlcState::default()
        };
        let mut procedure =
            SlcInitProcedure::new_at_state(AgFeaturesReceived { state: state.clone() });
        let update = at::Command::CindTest {};
        // Both parties support codec negotiation, but we receive an invalid HF message.
        assert_matches!(procedure.hf_update(update, &mut state), ProcedureRequest::Error(_));
    }

    #[test]
    fn supported_features_received_transition_with_no_codec_support() {
        // HF doesn't support codec negotiation.
        let mut state = SlcState {
            hf_features: HfFeatures::NR_EC,
            ag_features: AgFeatures::CODEC_NEGOTIATION,
            ..SlcState::default()
        };

        let mut procedure =
            SlcInitProcedure::new_at_state(AgFeaturesReceived { state: state.clone() });

        // Since one party doesn't support codec negotiation, we expect the next update to
        // be a request for the AG supported indicators.
        let update = at::Command::CindTest {};
        assert_matches!(procedure.hf_update(update, &mut state), ProcedureRequest::SendMessages(_));
    }

    #[test]
    fn supported_features_received_transition_unexpected_update_with_no_codec_support() {
        // AG doesn't support codec negotiation.
        let mut state = SlcState {
            hf_features: HfFeatures::CODEC_NEGOTIATION,
            ag_features: AgFeatures::NR_EC,
            ..SlcState::default()
        };
        let mut procedure =
            SlcInitProcedure::new_at_state(AgFeaturesReceived { state: state.clone() });
        let update = at::Command::Bac { codecs: Vec::new() };
        // One party doesn't support codec negotiation, so it is an error if the HF sends
        // a codec negotiation AT message.
        assert_matches!(procedure.hf_update(update, &mut state), ProcedureRequest::Error(_));
    }

    #[test]
    fn indicator_status_enable_with_invalid_mode_returns_error() {
        let mut state = SlcState {
            hf_features: HfFeatures::all(),
            ag_features: AgFeatures::all(),
            ..SlcState::default()
        };
        let mut procedure =
            SlcInitProcedure::new_at_state(AgIndicatorStatusReceived { state: state.clone() });

        // `mode` = 4 is invalid.
        let invalid_enable = at::Command::Cmer { mode: 4, keyp: 0, disp: 0, ind: 1 };
        assert_matches!(
            procedure.hf_update(invalid_enable, &mut state),
            ProcedureRequest::Error(Error::InvalidHfArgument(_))
        );
    }

    #[test]
    fn indicator_status_enable_with_invalid_ind_returns_error() {
        let mut state = SlcState {
            hf_features: HfFeatures::all(),
            ag_features: AgFeatures::all(),
            ..SlcState::default()
        };
        let mut procedure =
            SlcInitProcedure::new_at_state(AgIndicatorStatusReceived { state: state.clone() });

        // `ind` = 9 is invalid.
        let invalid_enable =
            at::Command::Cmer { mode: SUPPORTED_EVENT_REPORTING_MODE, keyp: 0, disp: 0, ind: 9 };
        assert_matches!(
            procedure.hf_update(invalid_enable, &mut state),
            ProcedureRequest::Error(Error::InvalidHfArgument(_))
        );
    }

    /// Validates the entire mandatory state machine for the SLCI Procedure, see
    /// Section 4.2.1.6 of HFP v1.8 for the mandatory steps. We can trigger the mandatory
    /// sequence of operations by specifying the lack of codec support, 3-way calling, and
    /// HF indicators in either the AG or HF.
    #[test]
    fn validate_mandatory_procedure_state_machine() {
        let mut slc_proc = SlcInitProcedure::new();
        let mut state = SlcState::default();

        assert_matches!(slc_proc.marker(), ProcedureMarker::SlcInitialization);
        assert!(!slc_proc.is_terminated());

        // Because the HF and AG don't both support the optional feature flags,
        // we don't expect to trigger any of the conditional state transitions in the
        // procedure.
        let hf_features = HfFeatures::CODEC_NEGOTIATION;
        let ag_features = AgFeatures::IN_BAND_RING;

        // First update should be an HF Feature request.
        let update1 = at::Command::Brsf { features: hf_features.bits() as i64 };
        assert_matches!(
            slc_proc.hf_update(update1, &mut state),
            ProcedureRequest::Info(InformationRequest::GetAgFeatures { .. })
        );

        // Next update should be an AG Feature response.
        let update2 = AgUpdate::Features(ag_features);
        assert_matches!(slc_proc.ag_update(update2, &mut state), ProcedureRequest::SendMessages(_));

        // Since the AG doesn't support codec negotiation (see `ag_features`), we expect to
        // skip to the Hf Indicator support stage.
        let update3 = at::Command::CindTest {};
        assert_matches!(slc_proc.hf_update(update3, &mut state), ProcedureRequest::SendMessages(_));

        // We then expect the HF to request the indicator status which will result
        // in the procedure asking the AG for the status.
        let update4 = at::Command::CindRead {};
        assert_matches!(
            slc_proc.hf_update(update4, &mut state),
            ProcedureRequest::Info(InformationRequest::GetAgIndicatorStatus { .. })
        );
        let update5 = AgUpdate::IndicatorStatus(IndicatorStatus::default());
        assert_matches!(slc_proc.ag_update(update5, &mut state), ProcedureRequest::SendMessages(_));

        // Lastly, the HF should request to enable the indicator status update on the AG.
        let update6 = at::Command::Cmer { mode: 3, keyp: 0, disp: 0, ind: 1 };
        assert_matches!(slc_proc.hf_update(update6, &mut state), ProcedureRequest::SendMessages(_));

        // Since both the AG and HF don't support 3-way calling and HF-indicators flags, we
        // expect the procedure to be terminated.
        assert!(slc_proc.is_terminated());
    }

    /// Validates the entire state machine, including optional states, for the SLCI Procedure.
    /// See HFP v1.8 Section 4.2.1.6 for the complete state diagram.
    #[test]
    fn validate_optional_procedure_state_machine() {
        let mut slc_proc = SlcInitProcedure::new();
        let mut state = SlcState::default();
        let hf_features = HfFeatures::all();
        let ag_features = AgFeatures::all();

        // First update should be an HF Feature request.
        let update1 = at::Command::Brsf { features: hf_features.bits() as i64 };
        assert_matches!(
            slc_proc.hf_update(update1, &mut state),
            ProcedureRequest::Info(InformationRequest::GetAgFeatures { .. })
        );

        // Next update should be an AG Feature response.
        let update2 = AgUpdate::Features(ag_features);
        assert_matches!(slc_proc.ag_update(update2, &mut state), ProcedureRequest::SendMessages(_));

        let update3 = at::Command::Bac { codecs: vec![] };
        assert_matches!(slc_proc.hf_update(update3, &mut state), ProcedureRequest::SendMessages(_));

        let update4 = at::Command::CindTest {};
        assert_matches!(slc_proc.hf_update(update4, &mut state), ProcedureRequest::SendMessages(_));

        let update5 = at::Command::CindRead {};
        assert_matches!(
            slc_proc.hf_update(update5, &mut state),
            ProcedureRequest::Info(InformationRequest::GetAgIndicatorStatus { .. })
        );

        // Indicator status should be updated.
        let update6 = AgUpdate::IndicatorStatus(IndicatorStatus::default());
        assert_matches!(slc_proc.ag_update(update6, &mut state), ProcedureRequest::SendMessages(_));

        let update7 = at::Command::Cmer { mode: 3, keyp: 0, disp: 0, ind: 1 };
        assert_matches!(slc_proc.hf_update(update7, &mut state), ProcedureRequest::SendMessages(_));

        // Optional
        let update8 = at::Command::ChldTest {};
        assert_matches!(slc_proc.hf_update(update8, &mut state), ProcedureRequest::SendMessages(_));
        // Optional
        let update9 = at::Command::Bind {
            indicators: vec![
                at::BluetoothHFIndicator::BatteryLevel,
                at::BluetoothHFIndicator::EnhancedSafety,
            ],
        };
        assert_matches!(slc_proc.hf_update(update9, &mut state), ProcedureRequest::SendMessages(_));
        // Optional
        let update10 = at::Command::BindTest {};
        assert_matches!(
            slc_proc.hf_update(update10, &mut state),
            ProcedureRequest::SendMessages(_)
        );
        // Optional
        let update11 = at::Command::BindRead {};
        assert_matches!(
            slc_proc.hf_update(update11, &mut state),
            ProcedureRequest::SendMessages(_)
        );

        assert!(slc_proc.is_terminated());
    }

    #[test]
    fn unexpected_at_event_results_in_error() {
        let mut slc_proc = SlcInitProcedure::new();
        let mut state = SlcState::default();

        // We don't expect this AT command to be received in the starting
        // state of the SLC Initialization Procedure.
        let unexpected_update1 = at::Command::CindTest {};
        assert_matches!(
            slc_proc.hf_update(unexpected_update1, &mut state),
            ProcedureRequest::Error(_)
        );

        // Jump to a different state and test an unexpected update.
        let mut state =
            SlcState { hf_features: HfFeatures::CODEC_NEGOTIATION, ..SlcState::default() };
        slc_proc = SlcInitProcedure::new_at_state(HfFeaturesReceived);
        let unexpected_update2 = at::Command::Cmer { mode: 3, keyp: 0, disp: 0, ind: 1 };
        assert_matches!(
            slc_proc.hf_update(unexpected_update2, &mut state),
            ProcedureRequest::Error(_)
        );
    }

    /// Validates the result of the is_terminated() check on various input flags.
    // TODO: We should probably do some sort of comprehensive list of all permutations
    // of input flags.
    #[test]
    fn check_is_terminated_on_last_mandatory_step() {
        let mut state = AgIndicatorStatusEnableReceived { state: SlcState::default() };

        // HF and AG both support 3-way calling - shouldn't be done.
        state.state.hf_features.set(HfFeatures::THREE_WAY_CALLING, true);
        state.state.ag_features.set(AgFeatures::THREE_WAY_CALLING, true);
        assert!(!state.is_terminal());

        // HF/AG both support 3-way calling and HF-indicators - shouldn't be done.
        state.state.hf_features.set(HfFeatures::HF_INDICATORS, true);
        state.state.ag_features.set(AgFeatures::HF_INDICATORS, true);
        assert!(!state.is_terminal());

        // HF/AG both support only HF-indicators - shouldn't be done.
        state.state.hf_features.set(HfFeatures::THREE_WAY_CALLING, false);
        state.state.ag_features.set(AgFeatures::THREE_WAY_CALLING, false);
        assert!(!state.is_terminal());

        // HF supports 3-way calling / HF-indicators, but AG doesn't - should be done
        // since AG doesn't support anything.
        state.state = SlcState::default();
        state.state.hf_features.set(HfFeatures::THREE_WAY_CALLING, true);
        state.state.hf_features.set(HfFeatures::HF_INDICATORS, true);
        assert!(state.is_terminal());
    }
}
