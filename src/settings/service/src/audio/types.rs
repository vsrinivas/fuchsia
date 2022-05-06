// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::audio::ModifiedCounters;

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
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

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct AudioInputInfo {
    pub mic_mute: bool,
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct AudioInfo {
    pub streams: [AudioStream; 5],
    pub input: AudioInputInfo,
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
