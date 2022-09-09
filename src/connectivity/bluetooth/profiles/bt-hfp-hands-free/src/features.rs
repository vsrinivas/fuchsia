// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;

use crate::config::HandsFreeFeatureSupport;

/// Codec IDs. See HFP 1.8, Section 10 / Appendix B.
pub const CVSD: u8 = 0x01;
pub const MSBC: u8 = 0x02;

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
