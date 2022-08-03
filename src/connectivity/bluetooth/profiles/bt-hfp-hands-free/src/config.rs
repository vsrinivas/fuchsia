// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

#[derive(Clone, Copy)]
#[cfg_attr(test, derive(Default))]
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
        // TODO(fxbug/dev/104472):Read config file
        Ok(Self {
            ec_or_nr: false,
            call_waiting_or_three_way_calling: false,
            cli_presentation_capability: false,
            voice_recognition_activation: false,
            remote_volume_control: false,
            wide_band_speech: false,
            enhanced_voice_recognition: false,
            enhanced_voice_recognition_with_text: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    fn proper_creation_default_features() {
        let config = HandsFreeFeatureSupport::load().expect("Read config correctly");
        assert!(!config.ec_or_nr);
        assert!(!config.call_waiting_or_three_way_calling);
        assert!(!config.cli_presentation_capability);
        assert!(!config.voice_recognition_activation);
        assert!(!config.remote_volume_control);
        assert!(!config.wide_band_speech);
        assert!(!config.enhanced_voice_recognition);
        assert!(!config.enhanced_voice_recognition_with_text);
    }
}
