// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use tracing::warn;
use {at_commands as at, thiserror::Error};

use crate::peer::{
    service_level_connection::{Command, SlcState},
    slc_request::SlcRequest,
    update::AgUpdate,
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

/// Defines the implementation of the Hang Up Procedure.
pub mod hang_up;

/// Defines the implementation of the Hold Procedure.
pub mod hold;

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

/// Defines the implementation of the Transfer of HF Indicator Values Procedure.
pub mod transfer_hf_indicator;

/// Defines the implementation of the Volume Level Synchronization Procedure.
pub mod volume_synchronization;

/// Implements the Codec Connection Setup Procedure.
pub mod codec_connection_setup;

/// Defines the implementation of the the Audio Volume Control Procedure.
pub mod volume_control;

/// Defines the implementaion of several ways for the HF to initiate calls.
pub mod initiate_call;

/// Defines the Codec Support Procedure.
pub mod codec_support;

/// Defines the AG Indicator status read Procedure.
pub mod indicator_status;

/// Defines the HF Indicator status test and read Procedure.
pub mod hf_indicator_status;

use answer::AnswerProcedure;
use call_line_ident_notifications::CallLineIdentNotificationsProcedure;
use call_waiting_notifications::CallWaitingNotificationsProcedure;
use codec_connection_setup::CodecConnectionSetupProcedure;
use codec_support::CodecSupportProcedure;
use dtmf::DtmfProcedure;
use extended_errors::ExtendedErrorsProcedure;
use hang_up::HangUpProcedure;
use hf_indicator_status::SupportedHfIndicatorsProcedure;
use hold::{HoldProcedure, ThreeWaySupportProcedure};
use indicator_status::IndicatorStatusProcedure;
use indicators_activation::IndicatorsActivationProcedure;
use initiate_call::InitiateCallProcedure;
use nrec::NrecProcedure;
use phone_status::PhoneStatusProcedure;
use query_current_calls::QueryCurrentCallsProcedure;
use query_operator_selection::QueryOperatorProcedure;
use ring::RingProcedure;
use slc_initialization::SlcInitProcedure;
use subscriber_number_information::SubscriberNumberInformationProcedure;
use transfer_hf_indicator::TransferHfIndicatorProcedure;
use volume_control::VolumeControlProcedure;
use volume_synchronization::VolumeSynchronizationProcedure;

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

impl ProcedureError {
    pub fn info(&self) -> String {
        use ProcedureError::*;
        match self {
            UnexpectedAg(update) => format!("Unexpected AG Update: {}", update),
            UnparsableHf(_) => "Unparsable HF message".into(),
            UnexpectedHf(_) => "Unexpected HF command".into(),
            InvalidHfArgument(_) => "Invalid HF Argument".into(),
            UnexpectedRequest | AlreadyTerminated | NotImplemented | Channel(..) => {
                format!("{:?}", self)
            }
        }
    }

    /// Return true if the error was caused by the direct action of the peer sending a message.
    pub fn caused_by_peer(&self) -> bool {
        use ProcedureError::*;
        matches!(self, UnparsableHf(_) | UnexpectedHf(_) | InvalidHfArgument(_))
    }
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
    /// The Noise Reduction/Echo Cancellation procedure as defined in HFP v1.8 Section 4.24.
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
    /// The Hang Up procedure as defined in HFP v1.8 Sections 4.14 - 4.15
    HangUp,
    /// The Transfer of HF Indicator Values procedure as defined in HFP v1.8 Section 4.36.1.5.
    TransferHfIndicator,
    /// The Call Hold procedure as defined in HFP v1.8 Section 4.22
    Hold,
    /// The Audio Volume Control procedure as defined in HFP v.18 Section 4.29.1
    VolumeControl,
    /// Procedures for HF initiated calls defined in HFP v1.8 Sections 4.18-4.20
    InitiateCall,
    /// The Codec Connection Setup procedure as defined in HFP v1.8 Section 4.11.3.
    CodecConnectionSetup,
    /// The Indicators Read procedure as defined in HFP v1.8 Section 4.2.1.3.
    IndicatorStatus,
    /// The Codec Support procedure as defined in HFP v1.8 4.2.1.2.
    CodecSupport,
    /// The Call Hold and Multiparty support procedure as defined in HFP v1.8 4.2.1.3
    ThreeWaySupport,
    /// The HF Indicators read procedure as defined in HFP v1.8 4.2.1.4
    SupportedHfIndicators,
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
            Self::HangUp => Box::new(HangUpProcedure::new()),
            Self::TransferHfIndicator => Box::new(TransferHfIndicatorProcedure::new()),
            Self::Hold => Box::new(HoldProcedure::new()),
            Self::VolumeControl => Box::new(VolumeControlProcedure::new()),
            Self::InitiateCall => Box::new(InitiateCallProcedure::new()),
            Self::CodecConnectionSetup => Box::new(CodecConnectionSetupProcedure::new()),
            Self::IndicatorStatus => Box::new(IndicatorStatusProcedure::new()),
            Self::CodecSupport => Box::new(CodecSupportProcedure::new()),
            Self::ThreeWaySupport => Box::new(ThreeWaySupportProcedure::new()),
            Self::SupportedHfIndicators => Box::new(SupportedHfIndicatorsProcedure::new()),
        }
    }

    /// Matches the HF `command` to the SLC Initialization procedure. Before initialization is
    /// complete, all commands should be routed to the initialization procedure.
    pub fn match_init_command(command: &at::Command) -> Result<Self, ProcedureError> {
        match command {
            at::Command::Brsf { .. }
            | at::Command::CindTest { .. }
            | at::Command::ChldTest { .. }
            | at::Command::BindTest { .. }
            | at::Command::BindRead { .. }
            | at::Command::CindRead { .. }
            | at::Command::Cmer { .. }
            | at::Command::Bac { .. } => Ok(Self::SlcInitialization),
            _ => {
                warn!("Received unexpected HF command before SLC Initialization was complete");
                Err(ProcedureError::NotImplemented)
            }
        }
    }

    /// Matches the HF `command` to a procedure. `initialized` represents the initialization state
    /// of the Service Level Connection.
    ///
    /// Returns an error if the command is unable to be matched.
    pub fn match_command(command: &at::Command, initialized: bool) -> Result<Self, ProcedureError> {
        if !initialized {
            return Self::match_init_command(command);
        } else {
            match command {
                at::Command::Brsf { .. } => Ok(Self::SlcInitialization),
                at::Command::BindTest { .. } | at::Command::BindRead { .. } => {
                    Ok(Self::SupportedHfIndicators)
                }
                at::Command::Bac { .. } => Ok(Self::CodecSupport),
                at::Command::CindTest { .. } | at::Command::CindRead { .. } => {
                    Ok(Self::IndicatorStatus)
                }
                at::Command::Cmer { .. } => Ok(Self::Indicators),
                at::Command::Nrec { .. } => Ok(Self::Nrec),
                at::Command::Cops { .. } | at::Command::CopsRead { .. } => {
                    Ok(Self::QueryOperatorSelection)
                }
                at::Command::Cmee { .. } => Ok(Self::ExtendedErrors),
                at::Command::Ccwa { .. } => Ok(Self::CallWaitingNotifications),
                at::Command::Clcc { .. } => Ok(Self::QueryCurrentCalls),
                at::Command::Clip { .. } => Ok(Self::CallLineIdentNotifications),
                at::Command::Cnum { .. } => Ok(Self::SubscriberNumberInformation),
                at::Command::Vgs { .. } | at::Command::Vgm { .. } => {
                    Ok(Self::VolumeSynchronization)
                }
                at::Command::Vts { .. } => Ok(Self::Dtmf),
                at::Command::Answer { .. } => Ok(Self::Answer),
                at::Command::Chup { .. } => Ok(Self::HangUp),
                at::Command::Biev { .. } => Ok(Self::TransferHfIndicator),
                at::Command::Bia { .. } => Ok(Self::Indicators),
                at::Command::Chld { .. } => Ok(Self::Hold),
                at::Command::ChldTest { .. } => Ok(Self::ThreeWaySupport),
                at::Command::AtdNumber { .. }
                | at::Command::AtdMemory { .. }
                | at::Command::Bldn { .. } => Ok(Self::InitiateCall),
                at::Command::Bcc { .. } | at::Command::Bcs { .. } => Ok(Self::CodecConnectionSetup),
                _ => Err(ProcedureError::NotImplemented),
            }
        }
    }
}

/// The requests generated by an HFP procedure as it progresses through its state machine.
#[derive(Debug)]
pub enum ProcedureRequest {
    /// AT messages to be sent to the peer (HF) - requires no response.
    SendMessages(Vec<at::Response>),

    /// Request for an action or information from the HFP component.
    Request(SlcRequest),

    /// Error from processing an update.
    Error(ProcedureError),

    /// No-op.
    None,
}

impl ProcedureRequest {
    /// Returns true if this request requires a response.
    pub fn requires_response(&self) -> bool {
        match &self {
            Self::Request(..) => true,
            _ => false,
        }
    }
}

impl From<Vec<at::Response>> for ProcedureRequest {
    fn from(messages: Vec<at::Response>) -> Self {
        Self::SendMessages(messages)
    }
}

impl From<SlcRequest> for ProcedureRequest {
    fn from(src: SlcRequest) -> Self {
        Self::Request(src)
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

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    /// A vec of responses converts to the expected request
    #[fuchsia::test]
    fn at_responses_to_procedure_request_conversion() {
        let messages = vec![at::Response::Ok, at::Response::Error];
        let request: ProcedureRequest = messages.into();
        assert_matches!(
            request,
            ProcedureRequest::SendMessages(messages)
                if messages == vec![at::Response::Ok, at::Response::Error]
        );
    }

    #[fuchsia::test]
    fn match_conditional_commands_based_on_slci() {
        let command = at::Command::Cmer { mode: 3, keyp: 0, disp: 0, ind: 1 };
        let marker = ProcedureMarker::match_command(&command, false).expect("command to match");
        assert_eq!(marker, ProcedureMarker::SlcInitialization);

        let command = at::Command::Cmer { mode: 3, keyp: 0, disp: 0, ind: 1 };
        let marker = ProcedureMarker::match_command(&command, true).expect("command to match");
        assert_eq!(marker, ProcedureMarker::Indicators);
    }
}
