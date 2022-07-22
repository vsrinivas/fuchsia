// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::types::{AudioInfo, AudioSettingSource, AudioStream, AudioStreamType};
use crate::base::SettingInfo;
use crate::config::default_settings::DefaultSetting;
use lazy_static::lazy_static;
use std::collections::HashMap;
use std::sync::Mutex;

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
pub type ModifiedCounters = HashMap<AudioStreamType, usize>;

lazy_static! {
    pub(crate) static ref DEFAULT_AUDIO_INFO: AudioInfo =
        AudioInfo { streams: DEFAULT_STREAMS, modified_counters: None };
}

lazy_static! {
    pub(crate) static ref AUDIO_DEFAULT_SETTINGS: Mutex<DefaultSetting<AudioInfo, &'static str>> =
        Mutex::new(DefaultSetting::new(
            Some(DEFAULT_AUDIO_INFO.clone()),
            "/config/data/audio_config_data.json",
        ));
}

pub(crate) fn create_default_modified_counters() -> ModifiedCounters {
    IntoIterator::into_iter([
        AudioStreamType::Background,
        AudioStreamType::Media,
        AudioStreamType::Interruption,
        AudioStreamType::SystemAgent,
        AudioStreamType::Communication,
    ])
    .map(|stream_type| (stream_type, 0))
    .collect()
}

pub(crate) const fn create_default_audio_stream(stream_type: AudioStreamType) -> AudioStream {
    AudioStream {
        stream_type,
        source: AudioSettingSource::User,
        user_volume_level: DEFAULT_VOLUME_LEVEL,
        user_volume_muted: DEFAULT_VOLUME_MUTED,
    }
}

/// Returns a default audio [`AudioInfo`] that is derived from
/// [`DEFAULT_AUDIO_INFO`] with any fields specified in the
/// audio configuration set.
///
/// [`DEFAULT_AUDIO_INFO`]: static@DEFAULT_AUDIO_INFO
pub(crate) fn default_audio_info() -> AudioInfo {
    let mut default_audio_info: AudioInfo = DEFAULT_AUDIO_INFO.clone();

    if let Ok(Some(audio_configuration)) = AUDIO_DEFAULT_SETTINGS.lock().unwrap().get_cached_value()
    {
        default_audio_info.streams = audio_configuration.streams;
    }
    default_audio_info
}

impl From<AudioInfo> for SettingInfo {
    fn from(audio: AudioInfo) -> SettingInfo {
        SettingInfo::Audio(audio)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async::TestExecutor;

    use crate::audio::types::{AudioInfoV1, AudioInfoV2};
    use crate::tests::helpers::move_executor_forward_and_get;
    use settings_storage::device_storage::DeviceStorageCompatible;

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
        modified_counters: None,
    };

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_audio_config() {
        let settings = default_audio_info();
        assert_eq!(CONFIG_AUDIO_INFO, settings);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_audio_info_migration_v1_to_v2() {
        let mut v1 = AudioInfoV1::default_value();
        let updated_mic_mute_val = !v1.input.mic_mute;
        v1.input.mic_mute = updated_mic_mute_val;

        let serialized_v1 = v1.serialize_to();

        let v2 = AudioInfoV2::deserialize_from(&serialized_v1);

        assert_eq!(v2.input.mic_mute, updated_mic_mute_val);
    }

    #[test]
    fn test_audio_info_migration_v2_to_current() {
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");

        let mut v2 = move_executor_forward_and_get(
            &mut executor,
            async { AudioInfoV2::default_value() },
            "Unable to get V2 default value",
        );
        let updated_mic_mute_val = !v2.input.mic_mute;
        v2.input.mic_mute = updated_mic_mute_val;

        let serialized_v2 = v2.serialize_to();

        let current = AudioInfo::deserialize_from(&serialized_v2);

        assert_eq!(current, AudioInfo::default_value());
    }

    #[test]
    fn test_audio_info_migration_v1_to_current() {
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");

        let mut v1 = move_executor_forward_and_get(
            &mut executor,
            async { AudioInfoV1::default_value() },
            "Unable to get V1 default value",
        );
        let updated_mic_mute_val = !v1.input.mic_mute;
        v1.input.mic_mute = updated_mic_mute_val;

        let serialized_v1 = v1.serialize_to();

        let current = AudioInfo::deserialize_from(&serialized_v1);

        assert_eq!(current, AudioInfo::default_value());
    }
}
