// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{AttachError, Inspect},
};

/// Configuration of optional feature support.
/// This list of features is derived from the optional features in HFP v1.8, Table 3.1.
#[derive(Clone, Copy, Debug)]
#[cfg_attr(test, derive(Default))]
pub struct AudioGatewayFeatureSupport {
    pub reject_incoming_voice_call: bool,
    pub three_way_calling: bool,
    pub in_band_ringtone: bool,
    pub echo_canceling_and_noise_reduction: bool,
    pub voice_recognition: bool,
    pub attach_phone_number_to_voice_tag: bool,
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

impl From<hfp_profile_config::Config> for AudioGatewayFeatureSupport {
    fn from(src: hfp_profile_config::Config) -> Self {
        AudioGatewayFeatureSupport {
            reject_incoming_voice_call: src.reject_incoming_voice_call,
            three_way_calling: src.three_way_calling,
            // TODO(fxbug.dev/75538): not supported yet
            in_band_ringtone: false,
            echo_canceling_and_noise_reduction: src.echo_canceling_and_noise_reduction,
            // TODO(fxbug.dev/66016): not supported yet
            voice_recognition: false,
            attach_phone_number_to_voice_tag: src.attach_phone_number_to_voice_tag,
            // TODO(fxbug.dev/74091): not supported yet
            respond_and_hold: false,
            enhanced_call_controls: src.enhanced_call_controls,
            wide_band_speech: src.wide_band_speech,
            // TODO(fxbug.dev/66016): not supported yet
            enhanced_voice_recognition: false,
            // TODO(fxbug.dev/66016): not supported yet
            enhanced_voice_recognition_with_text: false,
        }
    }
}

impl AudioGatewayFeatureSupport {
    /// Load AudioGatewayFeatureSupport from package config data directory.
    pub fn load() -> Self {
        hfp_profile_config::Config::take_from_startup_handle().into()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_data_tree;

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
                respond_and_hold: false,
                enhanced_call_controls: false,
                wide_band_speech: false,
                enhanced_voice_recognition: false,
                enhanced_voice_recognition_with_text: false,
            }
        });
    }
}
