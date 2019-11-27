// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::default_settings::DefaultSetting,
    crate::registry::device_storage::DeviceStorageCompatible,
    crate::switchboard::base::{
        AudioInfo, AudioInputInfo, AudioSettingSource, AudioStream, AudioStreamType,
    },
    lazy_static::lazy_static,
    std::sync::Mutex,
};

const DEFAULT_MIC_MUTE: bool = false;
const DEFAULT_VOLUME_LEVEL: f32 = 0.5;
const DEFAULT_VOLUME_MUTED: bool = false;

const DEFAULT_STREAMS: [AudioStream; 5] = [
    create_default_audio_stream(AudioStreamType::Background),
    create_default_audio_stream(AudioStreamType::Media),
    create_default_audio_stream(AudioStreamType::Interruption),
    create_default_audio_stream(AudioStreamType::SystemAgent),
    create_default_audio_stream(AudioStreamType::Communication),
];

const DEFAULT_AUDIO_INPUT_INFO: AudioInputInfo = AudioInputInfo { mic_mute: DEFAULT_MIC_MUTE };

const DEFAULT_AUDIO_INFO: AudioInfo =
    AudioInfo { streams: DEFAULT_STREAMS, input: DEFAULT_AUDIO_INPUT_INFO };

const fn create_default_audio_stream(stream_type: AudioStreamType) -> AudioStream {
    AudioStream {
        stream_type: stream_type,
        source: AudioSettingSource::User,
        user_volume_level: DEFAULT_VOLUME_LEVEL,
        user_volume_muted: DEFAULT_VOLUME_MUTED,
    }
}

lazy_static! {
    pub static ref AUDIO_DEFAULT_SETTINGS: Mutex<DefaultSetting<AudioInfo>> =
        Mutex::new(DefaultSetting::new(
            DEFAULT_AUDIO_INFO,
            Some("/config/data/audio_config_data.json".to_string()),
        ));
}

pub fn default_audio_info() -> AudioInfo {
    AUDIO_DEFAULT_SETTINGS.lock().unwrap().get_default_value()
}

impl DeviceStorageCompatible for AudioInfo {
    const KEY: &'static str = "audio_info";

    fn default_value() -> Self {
        default_audio_info()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const CONFIG_AUDIO_INFO: AudioInfo = AudioInfo {
        streams: [
            AudioStream {
                stream_type: AudioStreamType::Background,
                source: AudioSettingSource::System,
                user_volume_level: 0.6,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Media,
                source: AudioSettingSource::System,
                user_volume_level: 0.7,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Interruption,
                source: AudioSettingSource::System,
                user_volume_level: 0.2,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::SystemAgent,
                source: AudioSettingSource::User,
                user_volume_level: 0.3,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Communication,
                source: AudioSettingSource::User,
                user_volume_level: 0.4,
                user_volume_muted: false,
            },
        ],
        input: AudioInputInfo { mic_mute: true },
    };

    #[test]
    fn test_audio_config() {
        let settings = default_audio_info();
        assert_eq!(CONFIG_AUDIO_INFO, settings);
    }
}
