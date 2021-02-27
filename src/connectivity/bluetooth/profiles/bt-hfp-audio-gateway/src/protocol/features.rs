// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;

use crate::config::AudioGatewayFeatureSupport;

bitflags! {
    /// Bitmap defined in HFP v1.8, 4.35.1 for use with the "+BRSF" AT result code.
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
    /// Bitmap defined in HFP v1.8, 4.35.1 for use with the "AT+BRSF" AT command.
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
        // Mnadatory if Wide Band Speech is supported. See HFP v1.8, Table 3.1, Note 4.
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
