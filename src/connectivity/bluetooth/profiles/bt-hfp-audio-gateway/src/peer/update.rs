// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {at_commands as at, std::iter::once};

use super::{
    calls::Call,
    gain_control::Gain,
    indicators::{AgIndicator, AgIndicators, HfIndicators},
    procedure::{
        query_current_calls::build_clcc_response,
        subscriber_number_information::build_cnum_response, ProcedureRequest,
    },
};

use crate::features::{AgFeatures, CodecId};

// TODO (fxbug.dev/74091): Add multiparty support.
// TODO (fxbug.dev/74093): Add Explicit Call Transfer support.
const THREE_WAY_SUPPORT: &[&str] = &["0", "1", "1X", "2", "2X"];

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

// TODO (fxbug.dev/72873): Replace with defined AT RING response.
pub const RING_BYTES: &[u8] = b"RING";

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
    /// The information of an IncomingRinging call.
    CallWaiting(Call),
    /// The volume to set the HF speaker to.
    SpeakerVolumeControl(Gain),
    /// The volume to set the HF speaker to.
    MicrophoneVolumeControl(Gain),
    /// Start the codec connection procedure.
    /// CodecId should only be None to initiate CodecNegotiation, and will be
    /// filled in by the CodecConnectionSetupProcedure.
    CodecSetup(Option<CodecId>),
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
            AgUpdate::CallWaiting(call) => {
                vec![at::success(at::Success::Ccwa {
                    ty: call.number.type_(),
                    number: call.number.into(),
                })]
            }
            AgUpdate::SpeakerVolumeControl(gain) => {
                vec![at::success(at::Success::Vgs { level: gain.into() })]
            }
            AgUpdate::MicrophoneVolumeControl(gain) => {
                vec![at::success(at::Success::Vgm { level: gain.into() })]
            }
            AgUpdate::CodecSetup(selected) => {
                let codec = selected.expect("must be selected when sent").into();
                vec![at::success(at::Success::Bcs { codec })]
            }
        }
        .into()
    }
}

impl From<Result<(), ()>> for AgUpdate {
    fn from(r: Result<(), ()>) -> Self {
        if r.is_ok() {
            AgUpdate::Ok
        } else {
            AgUpdate::Error
        }
    }
}
