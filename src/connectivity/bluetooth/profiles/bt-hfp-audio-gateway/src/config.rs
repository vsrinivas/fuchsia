// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{AttachError, Inspect},
    serde::Deserialize,
    std::{fs::File, io::Read},
};

pub const SUPPORTED_FEATURES_FILE_PATH: &str = "/pkg/data/supported_features.config";

/// Configuration of optional feature support.
/// This list of features is derived from the optional features in HFP v1.8, Table 3.1.
#[derive(Deserialize, Clone, Copy, Debug)]
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

impl Inspect for AudioGatewayFeatureSupport {
    /// iattach for AudioGatewayFeatureSupport creates an immutable record of
    /// the inspect data without the need to store the node separately from the
    /// parent node. For this reason, the lifetime of the inspect data is tied
    /// to the lifetime of the `parent` node.
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        let init = |node: &inspect::Node| {
            node.record_bool("reject_incoming_voice_call", self.reject_incoming_voice_call);
            node.record_bool("three_way_calling", self.three_way_calling);
            node.record_bool("in_band_ringtone", self.in_band_ringtone);
            node.record_bool(
                "echo_canceling_and_noise_reduction",
                self.echo_canceling_and_noise_reduction,
            );
            node.record_bool("voice_recognition", self.voice_recognition);
            node.record_bool(
                "attach_phone_number_to_voice_tag",
                self.attach_phone_number_to_voice_tag,
            );
            node.record_bool("remote_audio_volume_control", self.remote_audio_volume_control);
            node.record_bool("respond_and_hold", self.respond_and_hold);
            node.record_bool("enhanced_call_controls", self.enhanced_call_controls);
            node.record_bool("wide_band_speech", self.wide_band_speech);
            node.record_bool("enhanced_voice_recognition", self.enhanced_voice_recognition);
            node.record_bool(
                "enhanced_voice_recognition_with_text",
                self.enhanced_voice_recognition_with_text,
            );
        };
        let _child = parent.record_child(name.as_ref(), init);
        Ok(())
    }
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
    use fuchsia_inspect::assert_data_tree;

    #[fuchsia::test]
    fn successful_deserialization_of_configuration_file() {
        let config = AudioGatewayFeatureSupport::load().expect("Parse config file correctly");
        assert!(!config.reject_incoming_voice_call);
        assert!(config.three_way_calling);
        assert!(!config.in_band_ringtone);
        assert!(config.echo_canceling_and_noise_reduction);
        assert!(!config.voice_recognition);
        assert!(!config.attach_phone_number_to_voice_tag);
        assert!(config.remote_audio_volume_control);
        assert!(!config.respond_and_hold);
        assert!(!config.enhanced_call_controls);
        assert!(!config.wide_band_speech);
        assert!(!config.enhanced_voice_recognition);
        assert!(!config.enhanced_voice_recognition_with_text);
    }

    #[fuchsia::test]
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

    #[fuchsia::test]
    fn expected_inspect_tree() {
        let inspector = inspect::Inspector::new();
        assert_data_tree!(inspector, root: {});

        let mut config = AudioGatewayFeatureSupport::default();
        config.three_way_calling = true;
        config.in_band_ringtone = true;

        let _ = config.iattach(&inspector.root(), "audio_gateway_feature_support").unwrap();
        assert_data_tree!(inspector, root: {
            audio_gateway_feature_support: {
                reject_incoming_voice_call: false,
                three_way_calling: true,
                in_band_ringtone: true,
                echo_canceling_and_noise_reduction: false,
                voice_recognition: false,
                attach_phone_number_to_voice_tag: false,
                remote_audio_volume_control: false,
                respond_and_hold: false,
                enhanced_call_controls: false,
                wide_band_speech: false,
                enhanced_voice_recognition: false,
                enhanced_voice_recognition_with_text: false,
            }
        });
    }
}
