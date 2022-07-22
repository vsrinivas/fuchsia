// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

use crate::audio::{create_default_modified_counters, default_audio_info, ModifiedCounters};
use settings_storage::device_storage::DeviceStorageCompatible;

#[derive(PartialEq, Eq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum AudioSettingSource {
    User,
    System,
    SystemWithFeedback,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize, Hash, Eq)]
pub enum AudioStreamType {
    Background,
    Media,
    Interruption,
    SystemAgent,
    Communication,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct AudioStream {
    pub stream_type: AudioStreamType,
    pub source: AudioSettingSource,
    pub user_volume_level: f32,
    pub user_volume_muted: bool,
}

impl AudioStream {
    pub(crate) fn has_finite_volume_level(&self) -> bool {
        self.user_volume_level.is_finite()
    }
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub struct SetAudioStream {
    pub stream_type: AudioStreamType,
    pub source: AudioSettingSource,
    pub user_volume_level: Option<f32>,
    pub user_volume_muted: Option<bool>,
}

impl SetAudioStream {
    pub(crate) fn has_finite_volume_level(&self) -> bool {
        self.user_volume_level.map(|v| v.is_finite()).unwrap_or(true)
    }

    pub(crate) fn is_valid_payload(&self) -> bool {
        self.user_volume_level.is_some() || self.user_volume_muted.is_some()
    }
}

impl From<AudioStream> for SetAudioStream {
    fn from(stream: AudioStream) -> Self {
        Self {
            stream_type: stream.stream_type,
            source: stream.source,
            user_volume_level: Some(stream.user_volume_level),
            user_volume_muted: Some(stream.user_volume_muted),
        }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct AudioInfo {
    pub streams: [AudioStream; 5],
    pub modified_counters: Option<ModifiedCounters>,
}

impl AudioInfo {
    /// Selectively replaces an existing stream of the same type with the one
    /// provided. The `AudioInfo` is left intact if that stream type does not
    /// exist.
    #[cfg(test)]
    pub(crate) fn replace_stream(&mut self, stream: AudioStream) {
        if let Some(s) = self.streams.iter_mut().find(|s| s.stream_type == stream.stream_type) {
            *s = stream;
        }
    }
}

impl DeviceStorageCompatible for AudioInfo {
    const KEY: &'static str = "audio_info";

    fn default_value() -> Self {
        default_audio_info()
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(value).unwrap_or_else(|_| Self::from(AudioInfoV2::deserialize_from(value)))
    }
}

////////////////////////////////////////////////////////////////
/// Past versions of AudioInfo.
////////////////////////////////////////////////////////////////

#[derive(PartialEq, Eq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct AudioInputInfo {
    pub mic_mute: bool,
}

/// The following struct should never be modified. It represents an old
/// version of the audio settings.
#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct AudioInfoV2 {
    pub streams: [AudioStream; 5],
    pub input: AudioInputInfo,
    pub modified_counters: Option<ModifiedCounters>,
}

impl DeviceStorageCompatible for AudioInfoV2 {
    const KEY: &'static str = "audio_info";

    fn default_value() -> Self {
        AudioInfoV2 {
            streams: default_audio_info().streams,
            input: AudioInputInfo { mic_mute: false },
            modified_counters: None,
        }
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(value).unwrap_or_else(|_| Self::from(AudioInfoV1::deserialize_from(value)))
    }
}

impl From<AudioInfoV2> for AudioInfo {
    fn from(v2: AudioInfoV2) -> AudioInfo {
        AudioInfo { streams: v2.streams, modified_counters: v2.modified_counters }
    }
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
        AudioInfoV1 {
            streams: default_audio_info().streams,
            input: AudioInputInfo { mic_mute: false },
            modified_timestamps: None,
        }
    }
}

impl From<AudioInfoV1> for AudioInfoV2 {
    fn from(v1: AudioInfoV1) -> Self {
        AudioInfoV2 {
            streams: v1.streams,
            input: v1.input,
            modified_counters: Some(create_default_modified_counters()),
        }
    }
}
