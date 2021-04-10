// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    at_commands as at,
    std::{fmt, iter::once},
    thiserror::Error,
};

use crate::{
    peer::{
        calls::Call,
        gain_control::Gain,
        service_level_connection::{Command, SlcState},
    },
    protocol::{
        features::AgFeatures,
        indicators::{AgIndicator, AgIndicators, HfIndicators},
    },
};

/// Defines the implementation of the Answer Procedure.
pub mod answer;

/// Defines the implementation of the DTMF Procedure.
pub mod dtmf;

/// Defines the implementation of the Call Waiting Notifications Procedure.
pub mod call_waiting_notifications;

/// Defines the implementation of the Call Line Identification Notifications Procedure.
pub mod call_line_ident_notifications;

/// Defines the implementation of the Report Extended Audio Gateway Error Code Results Procedure.
pub mod extended_errors;

/// Defines the implementation of the SLC Initialization Procedure.
pub mod slc_initialization;

/// Defines the implementation of the Subscriber Number Information Procedure.
pub mod subscriber_number_information;

/// Defines the implementation of the NR/EC Procedure.
pub mod nrec;

/// Defines the implementation of the Query List of Current Calls Procedure.
pub mod query_current_calls;

/// Defines the implementation of the Indicators Activation and Deactivation Procedure.
pub mod indicators_activation;

/// Defines the implementation of the Query Operator Selection Procedure.
pub mod query_operator_selection;

/// Defines the implementation of the Ring Procedure.
pub mod ring;

/// Defines the implementation of the Phone Status Procedures.
pub mod phone_status;

/// Defines the implementation of the Volume Level Synchronization Procedure.
pub mod volume_synchronization;

use answer::AnswerProcedure;
use call_line_ident_notifications::CallLineIdentNotificationsProcedure;
use call_waiting_notifications::CallWaitingNotificationsProcedure;
use dtmf::{DtmfCode, DtmfProcedure};
use extended_errors::ExtendedErrorsProcedure;
use indicators_activation::IndicatorsActivationProcedure;
use nrec::NrecProcedure;
use phone_status::PhoneStatusProcedure;
use query_current_calls::{build_clcc_response, QueryCurrentCallsProcedure};
use query_operator_selection::QueryOperatorProcedure;
use ring::RingProcedure;
use slc_initialization::SlcInitProcedure;
use subscriber_number_information::{build_cnum_response, SubscriberNumberInformationProcedure};
use volume_synchronization::VolumeSynchronizationProcedure;

const THREE_WAY_SUPPORT: &[&str] = &["0", "1", "1X", "2", "2X", "3", "4"];
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

// TODO (fxbug.dev/72873): Replace with defined AT RING repsonse.
const RING_BYTES: &[u8] = b"RING";

/// Errors that can occur during the operation of an HFP Procedure.
#[derive(Clone, Error, Debug)]
pub enum ProcedureError {
    #[error("Unexpected AG procedural update: {:?}", .0)]
    UnexpectedAg(AgUpdate),
    #[error("Unparsable HF procedural update: {:?}", .0)]
    UnparsableHf(at::DeserializeError),
    #[error("Unexpected HF procedural update: {:?}", .0)]
    UnexpectedHf(at::Command),
    #[error("Invalid HF argument in procedural update: {:?}", .0)]
    InvalidHfArgument(at::Command),
    #[error("Unexpected procedure request")]
    UnexpectedRequest,
    #[error("Procedure has already terminated")]
    AlreadyTerminated,
    #[error("Procedure not implemented")]
    NotImplemented,
    #[error("Error in the underlying service level connection: {:?}", .0)]
    Channel(fuchsia_zircon::Status),
}

impl From<fuchsia_zircon::Status> for ProcedureError {
    fn from(src: fuchsia_zircon::Status) -> Self {
        ProcedureError::Channel(src)
    }
}

impl From<&Command> for ProcedureError {
    fn from(src: &Command) -> Self {
        match src {
            Command::Ag(cmd) => Self::UnexpectedAg(cmd.clone()),
            Command::Hf(cmd) => Self::UnexpectedHf(cmd.clone()),
        }
    }
}

impl From<&at::Command> for ProcedureError {
    fn from(src: &at::Command) -> Self {
        Self::UnexpectedHf(src.clone())
    }
}

/// A unique identifier associated with an HFP procedure.
// TODO(fxbug.dev/70591): Add to this enum as more procedures are implemented.
#[derive(Copy, Clone, Debug, Hash, Eq, PartialEq)]
pub enum ProcedureMarker {
    /// The Service Level Connection Initialization procedure as defined in HFP v1.8 Section 4.2.
    SlcInitialization,
    /// The Transmit DTMF Code procedure as defined in HFP v1.8 Section 4.28.
    Dtmf,
    /// The Noise Reduction/Echo Cancelation procedure as defined in HFP v1.8 Section 4.24.
    Nrec,
    /// The Query Operator Selection procedure as defined in HFP v1.8 Section 4.8.
    QueryOperatorSelection,
    /// The Extended Audio Gateway Error Results Code as defined in HFP v1.8 Section 4.9.
    ExtendedErrors,
    /// The Extended Audio Gateway Error Results Code as defined in HFP v1.8 Section 4.21.
    CallWaitingNotifications,
    /// The Call Line Identification Notifications as defined in HFP v1.8 Section 4.23.
    CallLineIdentNotifications,
    /// The Transfer of Phone Status procedures as defined in HFP v1.8 Section 4.4 - 4.7.
    PhoneStatus,
    /// The Subscriber Number Information procedure as defined in HFP v1.8 Section 4.31.
    SubscriberNumberInformation,
    /// The Volume Level Synchronization procedure as defined in HFP v1.8 Section 4.29.2.
    VolumeSynchronization,
    /// The Query List of Current Calls procedure as defined in HFP v1.8 Section 4.32.1.
    QueryCurrentCalls,
    /// The Indicators Activation and Deactivation procedure as defined in HFP v1.8 Section 4.35.
    Indicators,
    /// The Ring procedure as defined in HFP v1.8 Section 4.13
    Ring,
    /// The Answer procedure as defined in HFP v1.8 Section 4.13
    Answer,
}

impl ProcedureMarker {
    /// Initializes a new procedure for the current marker.
    pub fn initialize(&self) -> Box<dyn Procedure> {
        match self {
            Self::SlcInitialization => Box::new(SlcInitProcedure::new()),
            Self::Nrec => Box::new(NrecProcedure::new()),
            Self::QueryOperatorSelection => Box::new(QueryOperatorProcedure::new()),
            Self::ExtendedErrors => Box::new(ExtendedErrorsProcedure::new()),
            Self::CallWaitingNotifications => Box::new(CallWaitingNotificationsProcedure::new()),
            Self::CallLineIdentNotifications => {
                Box::new(CallLineIdentNotificationsProcedure::new())
            }
            Self::PhoneStatus => Box::new(PhoneStatusProcedure::new()),
            Self::SubscriberNumberInformation => {
                Box::new(SubscriberNumberInformationProcedure::new())
            }
            Self::Dtmf => Box::new(DtmfProcedure::new()),
            Self::VolumeSynchronization => Box::new(VolumeSynchronizationProcedure::new()),
            Self::QueryCurrentCalls => Box::new(QueryCurrentCallsProcedure::new()),
            Self::Indicators => Box::new(IndicatorsActivationProcedure::new()),
            Self::Ring => Box::new(RingProcedure::new()),
            Self::Answer => Box::new(AnswerProcedure::new()),
        }
    }

    /// Matches the HF `command` to a procedure. Returns an error if the command is
    /// unable to be matched.
    pub fn match_command(command: &at::Command) -> Result<Self, ProcedureError> {
        match command {
            at::Command::Brsf { .. }
            | at::Command::Bac { .. }
            | at::Command::CindTest { .. }
            | at::Command::CindRead { .. }
            | at::Command::Cmer { .. }
            | at::Command::ChldTest { .. }
            | at::Command::BindTest { .. }
            | at::Command::BindRead { .. } => Ok(Self::SlcInitialization),
            at::Command::Nrec { .. } => Ok(Self::Nrec),
            at::Command::Cops { .. } | at::Command::CopsRead { .. } => {
                Ok(Self::QueryOperatorSelection)
            }
            at::Command::Cmee { .. } => Ok(Self::ExtendedErrors),
            at::Command::Ccwa { .. } => Ok(Self::CallWaitingNotifications),
            at::Command::Clcc { .. } => Ok(Self::QueryCurrentCalls),
            at::Command::Clip { .. } => Ok(Self::CallLineIdentNotifications),
            at::Command::Cnum { .. } => Ok(Self::SubscriberNumberInformation),
            at::Command::Vgs { .. } | at::Command::Vgm { .. } => Ok(Self::VolumeSynchronization),
            at::Command::Vts { .. } => Ok(Self::Dtmf),
            at::Command::Answer { .. } => Ok(Self::Answer),
            _ => Err(ProcedureError::NotImplemented),
        }
    }
}

/// Information requests - use the `response` fn to build a response to the request.
// TODO(fxbug.dev/70591): Add to this list once more procedures are implemented.
pub enum InformationRequest {
    GetAgFeatures { response: Box<dyn FnOnce(AgFeatures) -> AgUpdate> },

    GetAgIndicatorStatus { response: Box<dyn FnOnce(AgIndicators) -> AgUpdate> },

    GetNetworkOperatorName { response: Box<dyn FnOnce(Option<String>) -> AgUpdate> },

    GetSubscriberNumberInformation { response: Box<dyn FnOnce(Vec<String>) -> AgUpdate> },

    SetNrec { enable: bool, response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate> },

    SendDtmf { code: DtmfCode, response: Box<dyn FnOnce() -> AgUpdate> },

    SpeakerVolumeSynchronization { level: Gain, response: Box<dyn FnOnce() -> AgUpdate> },

    MicrophoneVolumeSynchronization { level: Gain, response: Box<dyn FnOnce() -> AgUpdate> },

    QueryCurrentCalls { response: Box<dyn FnOnce(Vec<Call>) -> AgUpdate> },

    Answer { response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate> },
}

impl From<&InformationRequest> for ProcedureMarker {
    fn from(src: &InformationRequest) -> ProcedureMarker {
        use InformationRequest::*;
        match src {
            GetAgFeatures { .. } | GetAgIndicatorStatus { .. } => Self::SlcInitialization,
            GetNetworkOperatorName { .. } => Self::QueryOperatorSelection,
            GetSubscriberNumberInformation { .. } => Self::SubscriberNumberInformation,
            SetNrec { .. } => Self::Nrec,
            SendDtmf { .. } => Self::Dtmf,
            SpeakerVolumeSynchronization { .. } | MicrophoneVolumeSynchronization { .. } => {
                Self::VolumeSynchronization
            }
            QueryCurrentCalls { .. } => Self::QueryCurrentCalls,
            Answer { .. } => Self::Answer,
        }
    }
}

impl fmt::Debug for InformationRequest {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s;
        let output = match &self {
            Self::GetAgFeatures { .. } => "GetAgFeatures",
            Self::GetAgIndicatorStatus { .. } => "GetAgIndicatorStatus",
            Self::GetSubscriberNumberInformation { .. } => "GetSubscriberNumberInformation",
            Self::SetNrec { enable: true, .. } => "SetNrec(enabled)",
            Self::SetNrec { enable: false, .. } => "SetNrec(disabled)",
            Self::GetNetworkOperatorName { .. } => "GetNetworkOperatorName",
            Self::QueryCurrentCalls { .. } => "QueryCurrentCalls ",
            // DTFM Code values are not displayed in Debug representation
            Self::SendDtmf { .. } => "SendDtmf",
            Self::SpeakerVolumeSynchronization { level, .. } => {
                s = format!("SpeakerVolumeSynchronization({:?})", level);
                &s
            }
            Self::MicrophoneVolumeSynchronization { level, .. } => {
                s = format!("MicrophoneVolumeSynchronization({:?})", level);
                &s
            }
            Self::Answer { .. } => "Answer",
        }
        .to_string();
        write!(f, "{}", output)
    }
}

/// The requests generated by an HFP procedure as it progresses through its state machine.
#[derive(Debug)]
pub enum ProcedureRequest {
    /// AT messages to be sent to the peer (HF) - requires no response.
    SendMessages(Vec<at::Response>),

    /// Request for information from the HFP component.
    Info(InformationRequest),

    /// Error from processing an update.
    Error(ProcedureError),

    /// No-op.
    None,
}

impl ProcedureRequest {
    /// Returns true if this request requires a response.
    pub fn requires_response(&self) -> bool {
        match &self {
            Self::Info(..) => true,
            _ => false,
        }
    }
}

impl From<Vec<at::Response>> for ProcedureRequest {
    fn from(messages: Vec<at::Response>) -> Self {
        Self::SendMessages(messages)
    }
}

impl From<InformationRequest> for ProcedureRequest {
    fn from(src: InformationRequest) -> Self {
        Self::Info(src)
    }
}

impl From<ProcedureError> for ProcedureRequest {
    fn from(src: ProcedureError) -> Self {
        Self::Error(src)
    }
}

/// An interface to interact with an HFP Procedure.
pub trait Procedure {
    /// Returns the unique identifier associated with this procedure.
    fn marker(&self) -> ProcedureMarker;

    /// Receive an HF `update` to progress the procedure. Returns a request
    /// to the update.
    ///
    /// `update` is the incoming AT message received from the HF.
    /// `state` is the shared state associated with the service level connection and may be
    /// modified when applying the update.
    ///
    /// There are no guarantees if `hf_update()` is called on a Procedure that is terminated
    /// (namely, `is_terminated()` returns true) and may result in an error request.
    /// The handling of unexpected or invalid updates is procedure dependent.
    ///
    /// Developers should ensure that the final request of a Procedure does not require
    /// a response.
    fn hf_update(&mut self, update: at::Command, _state: &mut SlcState) -> ProcedureRequest {
        ProcedureRequest::Error(ProcedureError::UnexpectedHf(update))
    }

    /// Receive an AG `update` to progress the procedure. Returns a request
    /// to the update.
    ///
    /// `update` is the incoming AT message received from the AG.
    /// `state` is the shared state associated with the service level connection and may be
    /// modified when applying the update.
    ///
    /// There are no guarantees if `ag_update()` is called on a Procedure that is terminated
    /// (namely, `is_terminated()` returns true) and may result in an error request.
    /// The handling of unexpected or invalid updates is procedure dependent.
    ///
    /// Developers should ensure that the final request of a Procedure does not require
    /// a response.
    fn ag_update(&mut self, update: AgUpdate, _state: &mut SlcState) -> ProcedureRequest {
        ProcedureRequest::Error(ProcedureError::UnexpectedAg(update))
    }

    /// Returns true if the Procedure is finished.
    fn is_terminated(&self) -> bool {
        false
    }
}

/// An update from the AG. Each update contains all the information necessary to generate a list of
/// AT responses.
#[derive(Debug, Clone)]
pub enum AgUpdate {
    /// HFP Features supported by the AG
    Features(AgFeatures),
    /// Three Way Calling support
    ThreeWaySupport,
    /// Current status of all AG Indicators
    IndicatorStatus(AgIndicators),
    /// An Update that contains no additional information
    Ok,
    /// An error occurred and should be communicated to the HF
    Error,
    /// Supported AG Indicators
    SupportedAgIndicators,
    /// The AG's supported HF indicators.
    SupportedHfIndicators { safety: bool, battery: bool },
    /// The current status (enabled/disabled) of the AG's supported HF indicators.
    SupportedHfIndicatorStatus(HfIndicators),
    /// The name of the network operator
    NetworkOperatorName(at::NetworkOperatorNameFormat, String),
    /// Phone status indicator
    PhoneStatusIndicator(AgIndicator),
    /// The AG's network subscriber number(s).
    SubscriberNumbers(Vec<String>),
    /// The list of ongoing calls.
    CurrentCalls(Vec<Call>),
    /// The information of an IncomingRinging call.
    Ring(Call),
}

impl From<AgUpdate> for ProcedureRequest {
    fn from(msg: AgUpdate) -> Self {
        match msg {
            AgUpdate::Features(features) => vec![
                at::success(at::Success::Brsf { features: features.bits() as i64 }),
                at::Response::Ok,
            ],
            AgUpdate::ThreeWaySupport => {
                let commands = THREE_WAY_SUPPORT.into_iter().map(|&s| s.into()).collect();
                vec![at::success(at::Success::Chld { commands }), at::Response::Ok]
            }
            AgUpdate::IndicatorStatus(status) => vec![
                at::success(at::Success::Cind {
                    service: status.service,
                    call: status.call.into(),
                    callsetup: status.callsetup as i64,
                    callheld: status.callheld as i64,
                    signal: status.signal as i64,
                    roam: status.roam,
                    battchg: status.battchg as i64,
                }),
                at::Response::Ok,
            ],
            AgUpdate::Ok => vec![at::Response::Ok],
            AgUpdate::Error => vec![at::Response::Error],
            AgUpdate::SupportedAgIndicators => {
                vec![at::Response::RawBytes(CIND_TEST_RESPONSE_BYTES.to_vec()), at::Response::Ok]
            }
            AgUpdate::SupportedHfIndicators { safety, battery } => {
                let mut indicators = vec![];
                if safety {
                    indicators.push(at::BluetoothHFIndicator::EnhancedSafety);
                }
                if battery {
                    indicators.push(at::BluetoothHFIndicator::BatteryLevel);
                }
                vec![at::success(at::Success::BindList { indicators }), at::Response::Ok]
            }
            AgUpdate::SupportedHfIndicatorStatus(hf_indicators) => hf_indicators.bind_response(),
            AgUpdate::NetworkOperatorName(format, name) => vec![
                at::success(at::Success::Cops {
                    format,
                    zero: 0,
                    // TODO(fxbug.dev/72112) Make this optional if it's not set.
                    operator: name,
                }),
                at::Response::Ok,
            ],
            AgUpdate::PhoneStatusIndicator(status) => {
                vec![status.into()]
            }
            AgUpdate::SubscriberNumbers(numbers) => {
                numbers.into_iter().map(build_cnum_response).chain(once(at::Response::Ok)).collect()
            }
            AgUpdate::CurrentCalls(calls) => calls
                .into_iter()
                .filter_map(build_clcc_response)
                .chain(once(at::Response::Ok))
                .collect(),
            AgUpdate::Ring(call) => {
                vec![
                    at::Response::RawBytes(RING_BYTES.to_vec()),
                    at::success(at::Success::Clip {
                        ty: call.number.type_(),
                        number: call.number.into(),
                    }),
                ]
            }
        }
        .into()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    /// A vec of responses converts to the expected request
    #[test]
    fn at_responses_to_procedure_request_conversion() {
        let messages = vec![at::Response::Ok, at::Response::Error];
        let request: ProcedureRequest = messages.into();
        assert_matches!(
            request,
            ProcedureRequest::SendMessages(messages)
                if messages == vec![at::Response::Ok, at::Response::Error]
        );
    }
}
