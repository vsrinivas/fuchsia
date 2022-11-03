// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use std::string::ToString;

use crate::config::AudioGatewayFeatureSupport;

bitflags! {
    /// Bitmap defined in HFP v1.8, Section 4.35.1 for use with the "+BRSF" AT result code.
    #[derive(Default)]
    pub struct AgFeatures: u32 {
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
    pub struct HfFeatures: u32 {
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

impl From<&AudioGatewayFeatureSupport> for AgFeatures {
    fn from(value: &AudioGatewayFeatureSupport) -> Self {
        let mut this = Self::empty();
        this.set(Self::THREE_WAY_CALLING, value.three_way_calling);
        this.set(Self::NR_EC, value.echo_canceling_and_noise_reduction);
        this.set(Self::VR, value.voice_recognition);
        this.set(Self::IN_BAND_RING, value.in_band_ringtone);
        this.set(Self::ATTACH_A_NUMBER_TO_VOICE_TAG, value.attach_phone_number_to_voice_tag);
        this.set(Self::REJECT_CALL, value.reject_incoming_voice_call);
        // Mandatory in HFP v1.8. See Table 3.1, Row 21a.
        this.set(Self::ENHANCED_CALL_STATUS, true);
        this.set(Self::ENHANCED_CALL_CONTROL, value.enhanced_call_controls);
        // Not configurable in Sapphire HFP Audio Gateway implementation.
        this.set(Self::EXTENDED_ERROR_RESULT_CODES, true);
        // Mandatory if Wide Band Speech is supported. See HFP v1.8, Table 3.1, Note 4.
        this.set(Self::CODEC_NEGOTIATION, value.wide_band_speech);
        // Not configurable in Sapphire HFP Audio Gateway implementation.
        this.set(Self::HF_INDICATORS, true);
        // Sapphire uses BR/EDR Secure Connections, so ESCO_S4 is mandatory.
        // See HFP v1.8 Table 5.8.
        this.set(Self::ESCO_S4, true);
        this.set(Self::EVR_STATUS, value.enhanced_voice_recognition);
        this.set(Self::VR_TEXT, value.enhanced_voice_recognition_with_text);
        this
    }
}

/// Codec IDs. See HFP 1.8, Section 10 / Appendix B.
#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub struct CodecId(u8);

impl CodecId {
    pub const CVSD: CodecId = CodecId(0x01);
    pub const MSBC: CodecId = CodecId(0x02);
}

impl From<u8> for CodecId {
    fn from(x: u8) -> Self {
        Self(x)
    }
}

impl Into<u8> for CodecId {
    fn into(self) -> u8 {
        self.0
    }
}

// Convenience conversions for interacting with AT library.
// TODO(fxbug.dev/71403): Remove this once AT library supports specifying correct widths.
impl Into<i64> for CodecId {
    fn into(self) -> i64 {
        self.0 as i64
    }
}

impl PartialEq<i64> for CodecId {
    fn eq(&self, other: &i64) -> bool {
        self.0 as i64 == *other
    }
}

impl std::fmt::Display for CodecId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            0x01 => write!(f, "{}", "CVSD"),
            0x02 => write!(f, "{}", "MSBC"),
            unknown => write!(f, "Unknown({:#x})", unknown),
        }
    }
}

pub fn codecs_to_string(codecs: &Vec<CodecId>) -> String {
    let codecs_string: Vec<String> = codecs.iter().map(ToString::to_string).collect();
    let codecs_string: Vec<&str> = codecs_string.iter().map(AsRef::as_ref).collect();
    let joined = codecs_string.join(", ");
    joined
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    fn codecs_format() {
        let cvsd = CodecId(0x1);
        let mbsc = CodecId(0x2);
        let unknown = CodecId(0xf);

        let cvsd_string = format!("{:}", cvsd);
        assert_eq!(String::from("CVSD"), cvsd_string);

        let mbsc_string = format!("{:}", mbsc);
        assert_eq!(String::from("MSBC"), mbsc_string);

        let unknown_string = format!("{:}", unknown);
        assert_eq!(String::from("Unknown(0xf)"), unknown_string);

        let joined_string = codecs_to_string(&vec![cvsd, mbsc, unknown]);
        assert_eq!(String::from("CVSD, MSBC, Unknown(0xf)"), joined_string);
    }
}
