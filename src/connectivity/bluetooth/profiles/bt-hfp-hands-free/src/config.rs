// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use hfp_hands_free_profile_config::Config;

#[derive(Clone, Copy, Default)]
pub struct HandsFreeFeatureSupport {
    pub ec_or_nr: bool,
    pub call_waiting_or_three_way_calling: bool,
    pub cli_presentation_capability: bool,
    pub voice_recognition_activation: bool,
    pub remote_volume_control: bool,
    pub wide_band_speech: bool,
    pub enhanced_voice_recognition: bool,
    pub enhanced_voice_recognition_with_text: bool,
}

impl HandsFreeFeatureSupport {
    pub fn load() -> Result<Self, Error> {
        let config = Config::take_from_startup_handle();
        Self::load_default_with_config(config)
    }

    pub fn load_default_with_config(str_config: Config) -> Result<Self, Error> {
        let mut config = Self::default();
        config.ec_or_nr = str_config.ec_or_nr;
        config.call_waiting_or_three_way_calling = str_config.call_waiting_or_three_way_calling;
        config.cli_presentation_capability = str_config.cli_presentation_capability;
        config.voice_recognition_activation = str_config.voice_recognition_activation;
        config.remote_volume_control = str_config.remote_volume_control;
        config.wide_band_speech = str_config.wide_band_speech;
        config.enhanced_voice_recognition = str_config.enhanced_voice_recognition;
        config.enhanced_voice_recognition_with_text =
            str_config.enhanced_voice_recognition_with_text;
        Ok(config)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    fn proper_creation_default_features() {
        let config = HandsFreeFeatureSupport::default();
        assert!(!config.ec_or_nr);
        assert!(!config.call_waiting_or_three_way_calling);
        assert!(!config.cli_presentation_capability);
        assert!(!config.voice_recognition_activation);
        assert!(!config.remote_volume_control);
        assert!(!config.wide_band_speech);
        assert!(!config.enhanced_voice_recognition);
        assert!(!config.enhanced_voice_recognition_with_text);
    }
    #[fuchsia::test]
    fn proper_creation_from_config() {
        let config = Config {
            ec_or_nr: false,
            call_waiting_or_three_way_calling: false,
            cli_presentation_capability: false,
            voice_recognition_activation: false,
            remote_volume_control: false,
            wide_band_speech: true,
            enhanced_voice_recognition: false,
            enhanced_voice_recognition_with_text: false,
        };
        let loaded_config = HandsFreeFeatureSupport::load_default_with_config(config).unwrap();
        let default = HandsFreeFeatureSupport::default();

        assert_eq!(loaded_config.ec_or_nr, default.ec_or_nr);
        assert_eq!(
            loaded_config.call_waiting_or_three_way_calling,
            default.call_waiting_or_three_way_calling
        );
        assert_eq!(loaded_config.cli_presentation_capability, default.cli_presentation_capability);
        assert_eq!(
            loaded_config.voice_recognition_activation,
            default.voice_recognition_activation
        );
        assert_eq!(loaded_config.remote_volume_control, default.remote_volume_control);
        assert_ne!(loaded_config.wide_band_speech, default.wide_band_speech);
        assert_eq!(loaded_config.enhanced_voice_recognition, default.enhanced_voice_recognition);
        assert_eq!(
            loaded_config.enhanced_voice_recognition_with_text,
            default.enhanced_voice_recognition_with_text
        );
    }
}
