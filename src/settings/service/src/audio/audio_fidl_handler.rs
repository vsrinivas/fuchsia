// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::DefaultHasher;
use std::convert::TryFrom;
use std::hash::{Hash, Hasher};

use fidl::prelude::*;
use fidl_fuchsia_settings::{
    AudioMarker, AudioRequest, AudioSetResponder, AudioSetResult, AudioSettings,
    AudioStreamSettingSource, AudioStreamSettings, AudioWatchResponder, Volume,
};
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use fuchsia_zircon as zx;

use fidl_fuchsia_media::AudioRenderUsage;
use fuchsia_trace as ftrace;

use crate::audio::types::{AudioSettingSource, AudioStream, AudioStreamType, SetAudioStream};
use crate::base::{SettingInfo, SettingType};
use crate::handler::base::Request;
use crate::ingress::{request, watch, Scoped};
use crate::job::source::{Error as JobError, ErrorResponder};
use crate::job::Job;
use crate::{trace, trace_guard};

/// Custom responder that wraps the real FIDL responder plus a tracing guard. The guard is stored
/// here so that it's active until a response is sent and this responder is dropped.
struct AudioSetTraceResponder {
    responder: AudioSetResponder,
    _guard: ftrace::AsyncScope,
}

impl request::Responder<Scoped<AudioSetResult>> for AudioSetTraceResponder {
    fn respond(self, Scoped(mut response): Scoped<AudioSetResult>) {
        let _ = self.responder.send(&mut response);
    }
}

impl ErrorResponder for AudioSetTraceResponder {
    fn id(&self) -> &'static str {
        "Audio_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.responder.send(&mut Err(error))
    }
}

impl request::Responder<Scoped<AudioSetResult>> for AudioSetResponder {
    fn respond(self, Scoped(mut response): Scoped<AudioSetResult>) {
        let _ = self.send(&mut response);
    }
}

impl watch::Responder<AudioSettings, zx::Status> for AudioWatchResponder {
    fn respond(self, response: Result<AudioSettings, zx::Status>) {
        match response {
            Ok(settings) => {
                let _ = self.send(settings);
            }
            Err(error) => {
                self.control_handle().shutdown_with_epitaph(error);
            }
        }
    }
}

impl TryFrom<AudioRequest> for Job {
    type Error = JobError;

    fn try_from(item: AudioRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            AudioRequest::Set { settings, responder } => {
                let id = ftrace::Id::new();
                let guard = trace_guard!(id, "audio fidl handler set");
                let responder = AudioSetTraceResponder { responder, _guard: guard };
                match to_request(settings, id) {
                    Ok(request) => {
                        Ok(request::Work::new(SettingType::Audio, request, responder).into())
                    }
                    Err(err) => {
                        fx_log_err!(
                            "{}: Failed to process request: {:?}",
                            AudioMarker::DEBUG_NAME,
                            err
                        );
                        Err(JobError::InvalidInput(Box::new(responder)))
                    }
                }
            }
            AudioRequest::Watch { responder } => {
                let mut hasher = DefaultHasher::new();
                "audio_watch".hash(&mut hasher);
                // Because we increment the modification counters stored in AudioInfo for
                // every change, clients would be notified for every change, even if the
                // streams are identical. A custom change function is used here so only
                // stream changes trigger the Watch notification.
                Ok(watch::Work::new_job_with_change_function(
                    SettingType::Audio,
                    responder,
                    watch::ChangeFunction::new(
                        hasher.finish(),
                        Box::new(move |old: &SettingInfo, new: &SettingInfo| match (old, new) {
                            (SettingInfo::Audio(old_info), SettingInfo::Audio(new_info)) => {
                                let mut old_streams = old_info.streams.iter();
                                let new_streams = new_info.streams.iter();
                                for new_stream in new_streams {
                                    let old_stream = old_streams
                                        .find(|stream| stream.stream_type == new_stream.stream_type)
                                        .expect("stream type should be found in existing streams");
                                    if old_stream != new_stream {
                                        return true;
                                    }
                                }
                                false
                            }
                            _ => false,
                        }),
                    ),
                ))
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", item);
                Err(JobError::Unsupported)
            }
        }
    }
}

impl From<SettingInfo> for AudioSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::Audio(info) = response {
            let mut streams = Vec::new();
            for stream in &info.streams {
                streams.push(AudioStreamSettings::from(*stream));
            }

            let mut audio_settings = AudioSettings::EMPTY;
            audio_settings.streams = Some(streams);
            audio_settings
        } else {
            panic!("incorrect value sent to audio");
        }
    }
}

impl From<AudioStream> for AudioStreamSettings {
    fn from(stream: AudioStream) -> Self {
        AudioStreamSettings {
            stream: Some(AudioRenderUsage::from(stream.stream_type)),
            source: Some(AudioStreamSettingSource::from(stream.source)),
            user_volume: Some(Volume {
                level: Some(stream.user_volume_level),
                muted: Some(stream.user_volume_muted),
                ..Volume::EMPTY
            }),
            ..AudioStreamSettings::EMPTY
        }
    }
}

impl From<AudioRenderUsage> for AudioStreamType {
    fn from(usage: AudioRenderUsage) -> Self {
        match usage {
            AudioRenderUsage::Background => AudioStreamType::Background,
            AudioRenderUsage::Media => AudioStreamType::Media,
            AudioRenderUsage::Interruption => AudioStreamType::Interruption,
            AudioRenderUsage::SystemAgent => AudioStreamType::SystemAgent,
            AudioRenderUsage::Communication => AudioStreamType::Communication,
        }
    }
}

impl From<AudioStreamType> for AudioRenderUsage {
    fn from(usage: AudioStreamType) -> Self {
        match usage {
            AudioStreamType::Background => AudioRenderUsage::Background,
            AudioStreamType::Media => AudioRenderUsage::Media,
            AudioStreamType::Interruption => AudioRenderUsage::Interruption,
            AudioStreamType::SystemAgent => AudioRenderUsage::SystemAgent,
            AudioStreamType::Communication => AudioRenderUsage::Communication,
        }
    }
}

impl From<AudioStreamSettingSource> for AudioSettingSource {
    fn from(source: AudioStreamSettingSource) -> Self {
        match source {
            AudioStreamSettingSource::User => AudioSettingSource::User,
            AudioStreamSettingSource::System => AudioSettingSource::System,
            AudioStreamSettingSource::SystemWithFeedback => AudioSettingSource::SystemWithFeedback,
        }
    }
}

impl From<AudioSettingSource> for AudioStreamSettingSource {
    fn from(source: AudioSettingSource) -> Self {
        match source {
            AudioSettingSource::User => AudioStreamSettingSource::User,
            AudioSettingSource::System => AudioStreamSettingSource::System,
            AudioSettingSource::SystemWithFeedback => AudioStreamSettingSource::SystemWithFeedback,
        }
    }
}

// Clippy warns about all variants starting with the same prefix `No`.
#[allow(clippy::enum_variant_names)]
#[derive(thiserror::Error, Debug, PartialEq)]
enum Error {
    #[error("request has no streams")]
    NoStreams,
    #[error("missing user_volume at stream {0}")]
    NoUserVolume(usize),
    #[error("missing user_volume.level and user_volume.muted at stream {0}")]
    MissingVolumeAndMuted(usize),
    #[error("missing stream at stream {0}")]
    NoStreamType(usize),
    #[error("missing source at stream {0}")]
    NoSource(usize),
}

fn to_request(settings: AudioSettings, id: ftrace::Id) -> Result<Request, Error> {
    trace!(id, "to_request");
    settings
        .streams
        .map(|streams| {
            streams
                .into_iter()
                .enumerate()
                .map(|(i, stream)| {
                    let user_volume = stream.user_volume.ok_or(Error::NoUserVolume(i))?;
                    let user_volume_level = user_volume.level;
                    let user_volume_muted = user_volume.muted;
                    let stream_type = stream.stream.ok_or(Error::NoStreamType(i))?.into();
                    let source = stream.source.ok_or(Error::NoSource(i))?.into();
                    let request = SetAudioStream {
                        stream_type,
                        source,
                        user_volume_level,
                        user_volume_muted,
                    };
                    if request.is_valid_payload() {
                        Ok(request)
                    } else {
                        Err(Error::MissingVolumeAndMuted(i))
                    }
                })
                .collect::<Result<Vec<_>, _>>()
                .map(|streams| Request::SetVolume(streams, id))
        })
        .unwrap_or(Err(Error::NoStreams))
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_STREAM: AudioStreamSettings = AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume { level: Some(0.6), muted: Some(false), ..Volume::EMPTY }),
        ..AudioStreamSettings::EMPTY
    };

    // Verifies that an entirely empty settings request results in an appropriate error.
    #[test]
    fn test_request_from_settings_empty() {
        let id = ftrace::Id::new();
        let request = to_request(AudioSettings::EMPTY, id);

        assert_eq!(request, Err(Error::NoStreams));
    }

    // Verifies that a settings request missing user volume info results in an appropriate error.
    #[test]
    fn test_request_missing_user_volume() {
        let mut stream = TEST_STREAM.clone();
        stream.user_volume = None;

        let audio_settings = AudioSettings { streams: Some(vec![stream]), ..AudioSettings::EMPTY };

        let id = ftrace::Id::new();
        let request = to_request(audio_settings, id);

        assert_eq!(request, Err(Error::NoUserVolume(0)));
    }

    // Verifies that a settings request missing the stream type results in an appropriate error.
    #[test]
    fn test_request_missing_stream_type() {
        let mut stream = TEST_STREAM.clone();
        stream.stream = None;

        let audio_settings = AudioSettings { streams: Some(vec![stream]), ..AudioSettings::EMPTY };

        let id = ftrace::Id::new();
        let request = to_request(audio_settings, id);

        assert_eq!(request, Err(Error::NoStreamType(0)));
    }

    // Verifies that a settings request missing the source results in an appropriate error.
    #[test]
    fn test_request_missing_source() {
        let mut stream = TEST_STREAM.clone();
        stream.source = None;

        let audio_settings = AudioSettings { streams: Some(vec![stream]), ..AudioSettings::EMPTY };

        let id = ftrace::Id::new();
        let request = to_request(audio_settings, id);

        assert_eq!(request, Err(Error::NoSource(0)));
    }

    // Verifies that a settings request missing both the user volume level and mute state results in
    // an appropriate error.
    #[test]
    fn test_request_missing_user_volume_level_and_muted() {
        let mut stream = TEST_STREAM.clone();
        stream.user_volume = Some(Volume { level: None, muted: None, ..Volume::EMPTY });

        let audio_settings = AudioSettings { streams: Some(vec![stream]), ..AudioSettings::EMPTY };

        let id = ftrace::Id::new();
        let request = to_request(audio_settings, id);

        assert_eq!(request, Err(Error::MissingVolumeAndMuted(0)));
    }
}
