// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::default_settings::DefaultSetting,
    crate::handler::device_storage::DeviceStorageCompatible,
    crate::switchboard::base::{
        AudioInfo, AudioInputInfo, AudioSettingSource, AudioStream, AudioStreamType,
    },
    lazy_static::lazy_static,
    serde::{Deserialize, Serialize},
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

/// A mapping from stream type to an arbitrary numerical value. This number will
/// change from the number sent in the previous update if the stream type's
/// volume has changed.
pub type ModifiedFlags = HashMap<AudioStreamType, usize>;

const DEFAULT_AUDIO_INPUT_INFO: AudioInputInfo = AudioInputInfo { mic_mute: DEFAULT_MIC_MUTE };

const DEFAULT_AUDIO_INFO: AudioInfo =
    AudioInfo { streams: DEFAULT_STREAMS, input: DEFAULT_AUDIO_INPUT_INFO, modified_flags: None };

lazy_static! {
    pub static ref AUDIO_DEFAULT_SETTINGS: Mutex<DefaultSetting<AudioInfo, &'static str>> =
        Mutex::new(DefaultSetting::new(
            Some(DEFAULT_AUDIO_INFO),
            "/config/data/audio_config_data.json",
        ));
}

pub fn create_default_modified_flags() -> ModifiedFlags {
    let mut flags = HashMap::new();
    let stream_types = [
        AudioStreamType::Background,
        AudioStreamType::Media,
        AudioStreamType::Interruption,
        AudioStreamType::SystemAgent,
        AudioStreamType::Communication,
    ];

    // The values inserted here are irrelevant. They are simply a starting
    // point.
    for stream_type in stream_types.iter() {
        flags.insert(*stream_type, 0);
    }
    flags
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
    AUDIO_DEFAULT_SETTINGS.lock().unwrap().get_default_value().expect("no audio default settings")
}

/// The following struct should never be modified. It represents an old
/// version of the audio settings.
#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct AudioInfoV1 {
    pub streams: [AudioStream; 5],
    pub input: AudioInputInfo,
    pub modified_timestamps: Option<HashMap<AudioStreamType, String>>,
}

impl DeviceStorageCompatible for AudioInfoV1 {
    const KEY: &'static str = "audio_info";

    fn default_value() -> Self {
        let stream_types = [
            create_default_audio_stream(AudioStreamType::Background),
            create_default_audio_stream(AudioStreamType::Media),
            create_default_audio_stream(AudioStreamType::Interruption),
            create_default_audio_stream(AudioStreamType::SystemAgent),
            create_default_audio_stream(AudioStreamType::Communication),
        ];

        AudioInfoV1 {
            streams: stream_types,
            input: AudioInputInfo { mic_mute: false },
            modified_timestamps: None,
        }
    }
}

impl DeviceStorageCompatible for AudioInfo {
    const KEY: &'static str = "audio_info";

    fn default_value() -> Self {
        default_audio_info()
    }

    fn deserialize_from(value: &String) -> Self {
        Self::extract(&value).unwrap_or_else(|_| Self::from(AudioInfoV1::deserialize_from(&value)))
    }
}

impl From<AudioInfoV1> for AudioInfo {
    fn from(v1: AudioInfoV1) -> Self {
        AudioInfo {
            streams: v1.streams,
            input: v1.input,
            modified_flags: Some(create_default_modified_flags()),
        }
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
        modified_flags: None,
    };

    #[test]
    fn test_audio_config() {
        let settings = default_audio_info();
        assert_eq!(CONFIG_AUDIO_INFO, settings);
    }

    #[test]
    fn test_audio_info_migration_v1_to_current() {
        let mut v1 = AudioInfoV1::default_value();
        let updated_mic_mute_val = !v1.input.mic_mute;
        v1.input.mic_mute = updated_mic_mute_val;

        let serialized_v1 = v1.serialize_to();

        let current = AudioInfo::deserialize_from(&serialized_v1);

        assert_eq!(current.input.mic_mute, updated_mic_mute_val);
    }
}
