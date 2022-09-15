// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;

use anyhow::{format_err, Error};
use bitflags::bitflags;

use crate::config::HandsFreeFeatureSupport;

/// Codec IDs. See HFP 1.8, Section 10 / Appendix B.
pub const CVSD: u8 = 0x01;
pub const MSBC: u8 = 0x02;

pub type CallIdx = usize;

/// Action to perform a call related supplementary services. During a call, the following procedures
/// shall be available for the subscriber to control the operation of Call Waiting or Call Hold;
///
/// See 3GPP TS 22.030 v16.0.0 / ETSI TS 122.030 v16.0.0
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum CallHoldAction {
    /// Releases all held calls or sets User Determined User Busy (UDUB) for a waiting call.
    ReleaseAllHeld,
    /// Releases all active calls (if any exist) and accepts the other (held or waiting) call.
    ReleaseAllActive,
    /// Releases call with specified CallIdx.
    ReleaseSpecified(CallIdx),
    /// Places all active calls (if any exist) on hold and accepts the other (held or waiting) call.
    HoldActiveAndAccept,
    /// Request private consultation mode with specified call (CallIdx). (Place all calls on hold
    /// EXCEPT the call indicated by CallIdx.)
    HoldAllExceptSpecified(CallIdx),
    /// Adds a held call to the conversation.
    AddCallToHeldConversation,
    /// Connects two calls and disconnects the subscriber from both calls. (optional for the HF).
    ExplicitCallTransfer,
}

bitflags! {
    /// Bitmap defined in HFP v1.8, Section 4.35.1 for use with the "+BRSF" AT result code.
    #[derive(Default)]
    pub struct AgFeatures: i64 {
        const THREE_WAY_CALLING            = 0b00_0000_0000_0001;
        const NR_EC                        = 0b00_0000_0000_0010;
        const VR                           = 0b00_0000_0000_0100;
        const IN_BAND_RING                 = 0b00_0000_0000_1000;
        const ATTACH_A_NUMBER_TO_VOICE_TAG = 0b00_0000_0001_0000;
        const REJECT_CALL                  = 0b00_0000_0010_0000;
        const ENHANCED_CALL_STATUS         = 0b00_0000_0100_0000;
        const ENHANCED_CALL_CONTROL        = 0b00_0000_1000_0000;
        const EXTENDED_ERROR_RESULT_CODES  = 0b00_0001_0000_0000;
        const CODEC_NEGOTIATION            = 0b00_0010_0000_0000;
        const HF_INDICATORS                = 0b00_0100_0000_0000;
        const ESCO_S4                      = 0b00_1000_0000_0000;
        const EVR_STATUS                   = 0b01_0000_0000_0000;
        const VR_TEXT                      = 0b10_0000_0000_0000;
    }
}
bitflags! {
    /// Bitmap defined in HFP v1.8, Section 4.35.1 for use with the "AT+BRSF" AT command.
    #[derive(Default)]
    pub struct HfFeatures: i64 {
        const NR_EC                        = 0b00_0000_0000_0001;
        const THREE_WAY_CALLING            = 0b00_0000_0000_0010;
        const CLI_PRESENTATION             = 0b00_0000_0000_0100;
        const VR_ACTIVATION                = 0b00_0000_0000_1000;
        const REMOTE_VOLUME_CONTROL        = 0b00_0000_0001_0000;
        const ENHANCED_CALL_STATUS         = 0b00_0000_0010_0000;
        const ENHANCED_CALL_CONTROL        = 0b00_0000_0100_0000;
        const CODEC_NEGOTIATION            = 0b00_0000_1000_0000;
        const HF_INDICATORS                = 0b00_0001_0000_0000;
        const ESCO_S4                      = 0b00_0010_0000_0000;
        const EVR_STATUS                   = 0b00_0100_0000_0000;
        const VR_TEXT                      = 0b00_1000_0000_0000;
    }
}

impl TryFrom<&str> for CallHoldAction {
    type Error = Error;
    fn try_from(cmd_str: &str) -> Result<Self, Self::Error> {
        let feature = match cmd_str {
            "0" => CallHoldAction::ReleaseAllHeld,
            "1" => CallHoldAction::ReleaseAllActive,
            "2" => CallHoldAction::HoldActiveAndAccept,
            cmd if cmd_str.starts_with("1") => {
                let idx = cmd[1..].parse::<usize>()?;
                CallHoldAction::ReleaseSpecified(idx)
            }
            cmd if cmd_str.starts_with("2") => {
                let idx = cmd[1..].parse::<usize>()?;
                CallHoldAction::HoldAllExceptSpecified(idx)
            }
            "3" => CallHoldAction::AddCallToHeldConversation,
            "4" => CallHoldAction::ExplicitCallTransfer,
            _ => {
                return Err(format_err!("Could not match command: {:?} to feature.", cmd_str));
            }
        };
        Ok(feature)
    }
}

impl From<HandsFreeFeatureSupport> for HfFeatures {
    fn from(value: HandsFreeFeatureSupport) -> Self {
        let mut this = Self::empty();
        this.set(Self::NR_EC, value.ec_or_nr);
        this.set(Self::THREE_WAY_CALLING, value.call_waiting_or_three_way_calling);
        this.set(Self::CLI_PRESENTATION, value.cli_presentation_capability);
        this.set(Self::VR_ACTIVATION, value.voice_recognition_activation);
        this.set(Self::REMOTE_VOLUME_CONTROL, value.remote_volume_control);
        this.set(Self::ENHANCED_CALL_STATUS, false);
        this.set(Self::ENHANCED_CALL_CONTROL, false);
        this.set(Self::CODEC_NEGOTIATION, value.wide_band_speech);
        this.set(Self::HF_INDICATORS, false);
        this.set(Self::ESCO_S4, true);
        this.set(Self::EVR_STATUS, value.enhanced_voice_recognition);
        this.set(Self::VR_TEXT, value.enhanced_voice_recognition_with_text);
        this
    }
}

/// Properly tries to parse the commands from the AG to retrieve the information about how the call
/// hold and multiparty services are supported. Will return an error if cannot parse one command
/// correctly.
pub fn extract_features_from_command(commands: &Vec<String>) -> Result<Vec<CallHoldAction>, Error> {
    let mut features = Vec::new();
    for command in commands {
        let feature = command.as_str().try_into()?;
        features.push(feature);
    }
    Ok(features)
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;

    #[fuchsia::test]
    fn commands_properly_translates_to_features() {
        let commands = vec![
            String::from("0"),
            String::from("1"),
            String::from("2"),
            String::from("11"),
            String::from("22"),
            String::from("3"),
            String::from("4"),
        ];
        let expected_results = vec![
            CallHoldAction::ReleaseAllHeld,
            CallHoldAction::ReleaseAllActive,
            CallHoldAction::HoldActiveAndAccept,
            CallHoldAction::ReleaseSpecified(1),
            CallHoldAction::HoldAllExceptSpecified(2),
            CallHoldAction::AddCallToHeldConversation,
            CallHoldAction::ExplicitCallTransfer,
        ];
        let result = extract_features_from_command(&commands);
        assert_matches!(result, Ok(_));
        assert_eq!(result.unwrap(), expected_results);
    }

    #[fuchsia::test]
    fn error_when_invalid_index() {
        let commands = vec![String::from("1A")];
        let result = extract_features_from_command(&commands);
        assert_matches!(result, Err(_));
    }

    #[fuchsia::test]
    fn error_when_feature_cannot_be_matched() {
        let commands = vec![String::from("5")];
        let result = extract_features_from_command(&commands);
        assert_matches!(result, Err(_));
    }

    #[fuchsia::test]
    fn error_when_one_feature_invalid() {
        // 5 is invalid command in list of valid features
        let commands = vec![String::from("0"), String::from("1"), String::from("5")];
        let result = extract_features_from_command(&commands);
        assert_matches!(result, Err(_));
    }
}
