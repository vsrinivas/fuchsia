// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

use crate::{
    features::AgFeatures,
    peer::{
        calls::{Call, CallAction},
        gain_control::Gain,
        indicators::{AgIndicators, HfIndicator},
        procedure::{dtmf::DtmfCode, hold::CallHoldAction, ProcedureMarker},
        update::AgUpdate,
    },
};

/// A request made by the Service Level Connection for more information from the
/// HFP component.
// TODO(fxbug.dev/70591): Add to this list once more procedures are implemented.
pub enum SlcRequest {
    GetAgFeatures {
        response: Box<dyn FnOnce(AgFeatures) -> AgUpdate>,
    },

    GetAgIndicatorStatus {
        response: Box<dyn FnOnce(AgIndicators) -> AgUpdate>,
    },

    GetNetworkOperatorName {
        response: Box<dyn FnOnce(Option<String>) -> AgUpdate>,
    },

    GetSubscriberNumberInformation {
        response: Box<dyn FnOnce(Vec<String>) -> AgUpdate>,
    },

    SetNrec {
        enable: bool,
        response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate>,
    },

    SendHfIndicator {
        indicator: HfIndicator,
        response: Box<dyn FnOnce() -> AgUpdate>,
    },

    SendDtmf {
        code: DtmfCode,
        response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate>,
    },

    SpeakerVolumeSynchronization {
        level: Gain,
        response: Box<dyn FnOnce() -> AgUpdate>,
    },

    MicrophoneVolumeSynchronization {
        level: Gain,
        response: Box<dyn FnOnce() -> AgUpdate>,
    },

    QueryCurrentCalls {
        response: Box<dyn FnOnce(Vec<Call>) -> AgUpdate>,
    },

    Answer {
        response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate>,
    },

    HangUp {
        response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate>,
    },

    InitiateCall {
        call_action: CallAction,
        response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate>,
    },

    Hold {
        command: CallHoldAction,
        response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate>,
    },

    /// Setup the Sco connection, as requested by the CodecConnectionSetup
    SynchronousConnectionSetup {
        response: Box<dyn FnOnce(Result<(), ()>) -> AgUpdate>,
    },

    RestartCodecConnectionSetup {
        response: Box<dyn FnOnce() -> AgUpdate>,
    },
}

impl From<&SlcRequest> for ProcedureMarker {
    fn from(src: &SlcRequest) -> ProcedureMarker {
        use SlcRequest::*;
        match src {
            GetAgFeatures { .. } | GetAgIndicatorStatus { .. } => Self::SlcInitialization,
            GetNetworkOperatorName { .. } => Self::QueryOperatorSelection,
            GetSubscriberNumberInformation { .. } => Self::SubscriberNumberInformation,
            SetNrec { .. } => Self::Nrec,
            SendDtmf { .. } => Self::Dtmf,
            SendHfIndicator { .. } => Self::TransferHfIndicator,
            SpeakerVolumeSynchronization { .. } | MicrophoneVolumeSynchronization { .. } => {
                Self::VolumeSynchronization
            }
            QueryCurrentCalls { .. } => Self::QueryCurrentCalls,
            Answer { .. } => Self::Answer,
            HangUp { .. } => Self::HangUp,
            Hold { .. } => Self::Hold,
            InitiateCall { .. } => Self::InitiateCall,
            SynchronousConnectionSetup { .. } => Self::CodecConnectionSetup,
            RestartCodecConnectionSetup { .. } => Self::CodecSupport,
        }
    }
}

impl fmt::Debug for SlcRequest {
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
            Self::SendHfIndicator { indicator, .. } => {
                s = format!("SendHfIndicator({:?})", indicator);
                &s
            }
            Self::SpeakerVolumeSynchronization { level, .. } => {
                s = format!("SpeakerVolumeSynchronization({:?})", level);
                &s
            }
            Self::MicrophoneVolumeSynchronization { level, .. } => {
                s = format!("MicrophoneVolumeSynchronization({:?})", level);
                &s
            }
            Self::Answer { .. } => "Answer",
            Self::HangUp { .. } => "HangUp",
            Self::Hold { .. } => "Hold",
            Self::InitiateCall { call_action, .. } => {
                s = format!("InitiateCall({:?})", call_action);
                &s
            }
            Self::SynchronousConnectionSetup { .. } => "SynchronousConnectionSetup",
            Self::RestartCodecConnectionSetup { .. } => "RestartCodecConnectionSetup",
        }
        .to_string();
        write!(f, "{}", output)
    }
}
