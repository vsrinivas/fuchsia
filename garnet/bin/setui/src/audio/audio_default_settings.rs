// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::default_settings::DefaultSetting,
    crate::handler::device_storage::DeviceStorageCompatible,
    crate::internal::common::default_time,
    crate::switchboard::base::{
        AudioInfo, AudioInputInfo, AudioSettingSource, AudioStream, AudioStreamType,
    },
    lazy_static::lazy_static,
    std::collections::HashMap,
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

/// Structure for storing last modified timestamps for each audio stream.
pub type ModifiedTimestamps = HashMap<AudioStreamType, String>;

const DEFAULT_AUDIO_INPUT_INFO: AudioInputInfo = AudioInputInfo { mic_mute: DEFAULT_MIC_MUTE };

const DEFAULT_AUDIO_INFO: AudioInfo = AudioInfo {
    streams: DEFAULT_STREAMS,
    input: DEFAULT_AUDIO_INPUT_INFO,
    modified_timestamps: None,
};

lazy_static! {
    pub static ref AUDIO_DEFAULT_SETTINGS: Mutex<DefaultSetting<AudioInfo, &'static str>> =
        Mutex::new(DefaultSetting::new(
            DEFAULT_AUDIO_INFO,
            Some("/config/data/audio_config_data.json"),
        ));
}

pub fn create_default_modified_timestamps() -> ModifiedTimestamps {
    let mut timestamps = HashMap::new();
    let stream_types = [
        AudioStreamType::Background,
        AudioStreamType::Media,
        AudioStreamType::Interruption,
        AudioStreamType::SystemAgent,
        AudioStreamType::Communication,
    ];
    for stream_type in stream_types.iter() {
        timestamps.insert(*stream_type, default_time().to_string());
    }
    timestamps
}

pub const fn create_default_audio_stream(stream_type: AudioStreamType) -> AudioStream {
    AudioStream {
        stream_type: stream_type,
        source: AudioSettingSource::User,
        user_volume_level: DEFAULT_VOLUME_LEVEL,
        user_volume_muted: DEFAULT_VOLUME_MUTED,
    }
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
        modified_timestamps: None,
    };

    #[test]
    fn test_audio_config() {
        let settings = default_audio_info();
        assert_eq!(CONFIG_AUDIO_INFO, settings);
    }
}
