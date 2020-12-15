// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    serde::Deserialize,
    std::{fs::File, io::Read},
};

pub const SUPPORTED_FEATURES_FILE_PATH: &str = "/config/data/supported_features.config";

/// Configuration of optional feature support.
/// This list of features is derived from the optional features in HFP v1.8, Table 3.1.
#[derive(Deserialize, Clone, Copy)]
#[cfg_attr(test, derive(Default))]
#[serde(deny_unknown_fields)]
pub struct AudioGatewayFeatureSupport {
    pub reject_incoming_voice_call: bool,
    pub three_way_calling: bool,
    pub in_band_ringtone: bool,
    pub echo_canceling_and_noise_reduction: bool,
    pub voice_recognition: bool,
    pub attach_phone_number_to_voice_tag: bool,
    pub remote_audio_volume_control: bool,
    pub respond_and_hold: bool,
    pub enhanced_call_controls: bool,
    pub wide_band_speech: bool,
    pub enhanced_voice_recognition: bool,
    pub enhanced_voice_recognition_with_text: bool,
}

impl AudioGatewayFeatureSupport {
    /// Load AudioGatewayFeatureSupport from package config data directory.
    pub fn load() -> Result<Self, Error> {
        Self::from_reader(File::open(SUPPORTED_FEATURES_FILE_PATH)?)
    }

    pub fn from_reader<R: Read>(config_reader: R) -> Result<Self, Error> {
        Ok(serde_json::from_reader(config_reader)?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn successful_deserialization_of_configuration_file() {
        AudioGatewayFeatureSupport::load().expect("Parse config file correctly");
    }

    #[test]
    fn unsuccessful_deserialization_of_malformed_config_data() {
        let invalid_json = br#"
        {
            "reject_incoming_voice_call" :
        }"#;
        assert!(AudioGatewayFeatureSupport::from_reader(&invalid_json[..]).is_err());

        let unknown_fields = br#"
            {
                "ABCD_UNKNOWN_FIELD" : true,
                "three_way_calling" : true,
                "echo_canceling_and_noise_reduction" : true,
                "voice_recognition" : false,
                "attach_phone_number_to_voice_tag" : true,
                "remote_audio_volume_control" : true,
                "respond_and_hold" : false,
                "enhanced_call_controls" : false,
                "wide_band_speech" : true,
                "in_band_ringtone": false,
                "enhanced_voice_recognition": false,
                "enhanced_voice_recognition_with_text": false
            }
        "#;
        assert!(AudioGatewayFeatureSupport::from_reader(&unknown_fields[..]).is_err());

        let missing_fields = br#"
            {
                "reject_incoming_voice_call" : true,
                "three_way_calling" : true,
            }
        "#;
        assert!(AudioGatewayFeatureSupport::from_reader(&missing_fields[..]).is_err());

        let incorrectly_typed_fields = br#"
            {
                "reject_incoming_voice_call" : 1,
                "three_way_calling" : "should be a bool not a string",
                "echo_canceling_and_noise_reduction" : true,
                "voice_recognition" : false,
                "attach_phone_number_to_voice_tag" : true,
                "remote_audio_volume_control" : true,
                "respond_and_hold" : false,
                "enhanced_call_controls" : false,
                "wide_band_speech" : true,
                "in_band_ringtone": false,
                "enhanced_voice_recognition": false,
                "enhanced_voice_recognition_with_text": false
            }
        "#;
        assert!(AudioGatewayFeatureSupport::from_reader(&incorrectly_typed_fields[..]).is_err());
    }
}
