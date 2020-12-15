// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Service Discovery Protocol (SDP) record definitions for the Hands-Free
//! and Audio Gateway roles.

use {bitflags::bitflags, fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_bluetooth::types::Uuid};

use crate::config::AudioGatewayFeatureSupport;

/// SDP Attribute ID for Network.
/// Defined in Assigned Numbers for SDP
/// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_ID_HFP_NETWORK: u16 = 0x0301;

/// SDP Attribute ID for the Supported Features of HFP.
/// Defined in Assigned Numbers for SDP
/// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_ID_HFP_SUPPORTED_FEATURES: u16 = 0x0311;

/// Major Version of HFP implementation
const PROFILE_MAJOR_VERSION: u8 = 1;

/// Minor Version of HFP implementation
const PROFILE_MINOR_VERSION: u8 = 8;

bitflags! {
    struct AudioGatewayFeaturesSdpAttribute: u16 {
        const THREE_WAY_CALLING                    = 0b0000_0001;
        const ECHO_CANCELATION_AND_NOISE_REDUCTION = 0b0000_0010;
        const VOICE_RECOGNITION                    = 0b0000_0100;
        const IN_BAND_RING                         = 0b0000_1000;
        const ATTACH_NUMBER_TO_VOICE_TAG           = 0b0001_0000;
        const WIDEBAND_SPEECH                      = 0b0010_0000;
        const ENHANCED_VOICE_RECOGNITION           = 0b0100_0000;
        const ENHANCED_VOICE_RECOGNITION_TEXT      = 0b1000_0000;

        /// Defined by HFP v1.8, Table 5.3: Service Record for the AG
        const DEFAULT = Self::THREE_WAY_CALLING.bits | Self::IN_BAND_RING.bits;
    }
}

impl From<AudioGatewayFeatureSupport> for AudioGatewayFeaturesSdpAttribute {
    fn from(features: AudioGatewayFeatureSupport) -> Self {
        let mut value = Self::empty();
        value.set(Self::THREE_WAY_CALLING, features.three_way_calling);
        value.set(
            Self::ECHO_CANCELATION_AND_NOISE_REDUCTION,
            features.echo_canceling_and_noise_reduction,
        );
        value.set(Self::IN_BAND_RING, features.in_band_ringtone);
        value.set(Self::ATTACH_NUMBER_TO_VOICE_TAG, features.attach_phone_number_to_voice_tag);
        value.set(Self::WIDEBAND_SPEECH, features.wide_band_speech);
        value.set(Self::VOICE_RECOGNITION, features.voice_recognition);
        value.set(Self::ENHANCED_VOICE_RECOGNITION, features.enhanced_voice_recognition);
        value.set(
            Self::ENHANCED_VOICE_RECOGNITION_TEXT,
            features.enhanced_voice_recognition_with_text,
        );
        value
    }
}

/// Make the SDP definition for the HFP Audio Gateway service.
/// See HFP v1.8, Table 5.3.
pub fn audio_gateway(features: AudioGatewayFeatureSupport) -> bredr::ServiceDefinition {
    let network_supports_reject_call = features.reject_incoming_voice_call;
    let supported_features = AudioGatewayFeaturesSdpAttribute::from(features).bits();

    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![
            Uuid::new16(bredr::ServiceClassProfileIdentifier::HandsfreeAudioGateway as u16).into(),
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
            profile_id: bredr::ServiceClassProfileIdentifier::HandsfreeAudioGateway,
            major_version: PROFILE_MAJOR_VERSION,
            minor_version: PROFILE_MINOR_VERSION,
        }]),
        additional_attributes: Some(vec![
            bredr::Attribute {
                id: ATTR_ID_HFP_NETWORK,
                element: bredr::DataElement::Uint8(network_supports_reject_call.into()),
            },
            bredr::Attribute {
                id: ATTR_ID_HFP_SUPPORTED_FEATURES,
                element: bredr::DataElement::Uint16(supported_features),
            },
        ]),
        ..bredr::ServiceDefinition::EMPTY
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Make sure all the flags are correctly mapped in the `From` implementation.
    /// Bit flags are independent so a test covering each flag independently is sufficient
    #[test]
    fn ag_features_sdp_attr_from_ag_features() {
        let three_way_calling =
            AudioGatewayFeatureSupport { three_way_calling: true, ..Default::default() };
        assert_eq!(AudioGatewayFeaturesSdpAttribute::THREE_WAY_CALLING, three_way_calling.into());

        let ec_nr = AudioGatewayFeatureSupport {
            echo_canceling_and_noise_reduction: true,
            ..Default::default()
        };
        assert_eq!(
            AudioGatewayFeaturesSdpAttribute::ECHO_CANCELATION_AND_NOISE_REDUCTION,
            ec_nr.into()
        );

        let in_band_ringtone =
            AudioGatewayFeatureSupport { in_band_ringtone: true, ..Default::default() };
        assert_eq!(AudioGatewayFeaturesSdpAttribute::IN_BAND_RING, in_band_ringtone.into());

        let vr = AudioGatewayFeatureSupport { voice_recognition: true, ..Default::default() };
        assert_eq!(AudioGatewayFeaturesSdpAttribute::VOICE_RECOGNITION, vr.into());

        let attach_phone_number_to_voice_tag = AudioGatewayFeatureSupport {
            attach_phone_number_to_voice_tag: true,
            ..Default::default()
        };
        assert_eq!(
            AudioGatewayFeaturesSdpAttribute::ATTACH_NUMBER_TO_VOICE_TAG,
            attach_phone_number_to_voice_tag.into()
        );

        let wide_band_speech =
            AudioGatewayFeatureSupport { wide_band_speech: true, ..Default::default() };
        assert_eq!(AudioGatewayFeaturesSdpAttribute::WIDEBAND_SPEECH, wide_band_speech.into());

        let enhanced_voice_recognition =
            AudioGatewayFeatureSupport { enhanced_voice_recognition: true, ..Default::default() };
        assert_eq!(
            AudioGatewayFeaturesSdpAttribute::ENHANCED_VOICE_RECOGNITION,
            enhanced_voice_recognition.into()
        );

        let enhanced_voice_recognition_with_text = AudioGatewayFeatureSupport {
            enhanced_voice_recognition_with_text: true,
            ..Default::default()
        };
        assert_eq!(
            AudioGatewayFeaturesSdpAttribute::ENHANCED_VOICE_RECOGNITION_TEXT,
            enhanced_voice_recognition_with_text.into()
        );
    }
}
