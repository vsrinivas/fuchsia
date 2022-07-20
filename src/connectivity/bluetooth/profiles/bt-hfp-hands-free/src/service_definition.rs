// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Service Discovery Protocol (SDP) record definitions for the Hands-Free
//! and Audio Gateway roles.

use bitflags::bitflags;
use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_bluetooth::types::Uuid;

use crate::config::HandsFreeFeatureSupport;

/// SDP Attribute ID for the Supported Features of HFP.
/// Defined in Assigned Numbers for SDP
/// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_ID_HFP_SUPPORTED_FEATURES: u16 = 0x0311;

/// Major Version of HFP implementation
const PROFILE_MAJOR_VERSION: u8 = 1;

/// Minor Version of HFP implementation
const PROFILE_MINOR_VERSION: u8 = 8;

bitflags! {
    struct HandsFreeFeaturesSdpAttribute: u16 {
        const ECHO_CANCELLATION_AND_NOISE_REDUCTION = 0b0000_0001;
        const CALL_WAITING_OR_THREE_WAY_CALLING     = 0b0000_0010;
        const CLI_PRESENTATION_CAPABILITY           = 0b0000_0100;
        const VOICE_RECOGNITION_ACTIVATION          = 0b0000_1000;
        const REMOTE_VOLUME_CONTROL                 = 0b0001_0000;
        const WIDEBAND_SPEECH                       = 0b0010_0000;
        const ENHANCED_VOICE_RECOGNITION            = 0b0100_0000;
        const EHANCED_VOICE_RECOGNITION_TEXT        = 0b1000_0000;

        /// Defined by HFP v1.8, Table 5.1: Service Record for the HF
        const DEFAULT = 0b0000_0000;
    }
}

impl From<HandsFreeFeatureSupport> for HandsFreeFeaturesSdpAttribute {
    fn from(features: HandsFreeFeatureSupport) -> Self {
        let mut value = Self::empty();
        value.set(Self::ECHO_CANCELLATION_AND_NOISE_REDUCTION, features.ec_or_nr);
        value.set(
            Self::CALL_WAITING_OR_THREE_WAY_CALLING,
            features.call_waiting_or_three_way_calling,
        );
        value.set(Self::CLI_PRESENTATION_CAPABILITY, features.cli_presentation_capability);
        value.set(Self::VOICE_RECOGNITION_ACTIVATION, features.voice_recognition_activation);
        value.set(Self::REMOTE_VOLUME_CONTROL, features.remote_volume_control);
        value.set(Self::WIDEBAND_SPEECH, features.wide_band_speech);
        value.set(Self::ENHANCED_VOICE_RECOGNITION, features.enhanced_voice_recognition);
        value.set(
            Self::EHANCED_VOICE_RECOGNITION_TEXT,
            features.enhanced_voice_recognition_with_text,
        );
        value
    }
}

/// Makes the SDP definition for the HFP Hands Free service
/// See HFP v1.8, Table 5.1
pub fn hands_free(features: HandsFreeFeatureSupport) -> bredr::ServiceDefinition {
    let supported_features = HandsFreeFeaturesSdpAttribute::from(features).bits();
    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![
            Uuid::new16(bredr::ServiceClassProfileIdentifier::Handsfree as u16).into(),
            Uuid::new16(bredr::ServiceClassProfileIdentifier::GenericAudio as u16).into(),
        ]),
        protocol_descriptor_list: Some(vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![],
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![],
            },
        ]),
        profile_descriptors: Some(vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::Handsfree,
            major_version: PROFILE_MAJOR_VERSION,
            minor_version: PROFILE_MINOR_VERSION,
        }]),
        additional_attributes: Some(vec![bredr::Attribute {
            id: ATTR_ID_HFP_SUPPORTED_FEATURES,
            element: bredr::DataElement::Uint16(supported_features),
        }]),
        ..bredr::ServiceDefinition::EMPTY
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Make sure all the flags are correctly mapped in the 'From' implementation.
    /// Bit flags are independent so a test covering each flag independently is sufficient
    #[fuchsia::test]
    fn hf_features_sdp_attr_from_hf_features() {
        let ec_nr = HandsFreeFeatureSupport { ec_or_nr: true, ..Default::default() };
        assert_eq!(
            HandsFreeFeaturesSdpAttribute::ECHO_CANCELLATION_AND_NOISE_REDUCTION,
            ec_nr.into()
        );
        let call_waiting_or_three_way_calling = HandsFreeFeatureSupport {
            call_waiting_or_three_way_calling: true,
            ..Default::default()
        };
        assert_eq!(
            HandsFreeFeaturesSdpAttribute::CALL_WAITING_OR_THREE_WAY_CALLING,
            call_waiting_or_three_way_calling.into()
        );
        let cli_presentation_capability =
            HandsFreeFeatureSupport { cli_presentation_capability: true, ..Default::default() };
        assert_eq!(
            HandsFreeFeaturesSdpAttribute::CLI_PRESENTATION_CAPABILITY,
            cli_presentation_capability.into()
        );
        let voice_recognition_activation =
            HandsFreeFeatureSupport { voice_recognition_activation: true, ..Default::default() };
        assert_eq!(
            HandsFreeFeaturesSdpAttribute::VOICE_RECOGNITION_ACTIVATION,
            voice_recognition_activation.into()
        );
        let remote_volume_control =
            HandsFreeFeatureSupport { remote_volume_control: true, ..Default::default() };
        assert_eq!(
            HandsFreeFeaturesSdpAttribute::REMOTE_VOLUME_CONTROL,
            remote_volume_control.into()
        );
        let wide_band_speech =
            HandsFreeFeatureSupport { wide_band_speech: true, ..Default::default() };
        assert_eq!(HandsFreeFeaturesSdpAttribute::WIDEBAND_SPEECH, wide_band_speech.into());
        let enhanced_voice_recognition =
            HandsFreeFeatureSupport { enhanced_voice_recognition: true, ..Default::default() };
        assert_eq!(
            HandsFreeFeaturesSdpAttribute::ENHANCED_VOICE_RECOGNITION,
            enhanced_voice_recognition.into()
        );
        let enhanced_voice_recognition_with_text = HandsFreeFeatureSupport {
            enhanced_voice_recognition_with_text: true,
            ..Default::default()
        };
        assert_eq!(
            HandsFreeFeaturesSdpAttribute::EHANCED_VOICE_RECOGNITION_TEXT,
            enhanced_voice_recognition_with_text.into()
        );
    }
}
