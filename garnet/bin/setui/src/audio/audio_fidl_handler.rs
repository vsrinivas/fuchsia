// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_hanging_get_responder,
    crate::fidl_process,
    crate::fidl_processor::RequestContext,
    crate::request_respond,
    crate::switchboard::base::{
        AudioSettingSource, AudioStream, AudioStreamType, SettingRequest, SettingResponse,
        SettingType,
    },
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_settings::{
        AudioMarker, AudioRequest, AudioSettings, AudioStreamSettingSource, AudioStreamSettings,
        AudioWatch2Responder, AudioWatchResponder, Error, Volume,
    },
    fuchsia_async as fasync,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
};

fidl_hanging_get_responder!(
    AudioMarker,
    AudioSettings,
    AudioWatchResponder,
    AudioSettings,
    AudioWatch2Responder,
);

impl From<SettingResponse> for AudioSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Audio(info) = response {
            let mut streams = Vec::new();
            for stream in info.streams.iter() {
                streams.push(AudioStreamSettings::from(stream.clone()));
            }

            let mut audio_settings = AudioSettings::empty();
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
            }),
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
        }
    }
}

impl From<AudioSettingSource> for AudioStreamSettingSource {
    fn from(source: AudioSettingSource) -> Self {
        match source {
            AudioSettingSource::User => AudioStreamSettingSource::User,
            AudioSettingSource::System => AudioStreamSettingSource::System,
        }
    }
}

fn to_request(settings: AudioSettings) -> Option<SettingRequest> {
    let mut request = None;
    if let Some(streams_value) = settings.streams {
        let mut streams = Vec::new();

        for stream in streams_value {
            let user_volume = stream.user_volume.unwrap();

            streams.push(AudioStream {
                stream_type: AudioStreamType::from(stream.stream.unwrap()),
                source: AudioSettingSource::from(stream.source.unwrap()),
                user_volume_level: user_volume.level.unwrap(),
                user_volume_muted: user_volume.muted.unwrap(),
            });
        }

        request = Some(SettingRequest::SetVolume(streams));
    }
    request
}

fidl_process!(
    Audio,
    SettingType::Audio,
    process_request,
    SettingType::Audio,
    AudioSettings,
    AudioWatch2Responder,
    process_request_2,
);

async fn process_request(
    context: RequestContext<AudioSettings, AudioWatchResponder>,
    req: AudioRequest,
) -> Result<Option<AudioRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        AudioRequest::Set { settings, responder } => {
            if let Some(request) = to_request(settings) {
                fasync::Task::spawn(async move {
                    request_respond!(
                        context,
                        responder,
                        SettingType::Audio,
                        request,
                        Ok(()),
                        Err(fidl_fuchsia_settings::Error::Failed),
                        AudioMarker::DEBUG_NAME
                    );
                })
                .detach();
            } else {
                responder
                    .send(&mut Err(Error::Unsupported))
                    .log_fidl_response_error(AudioMarker::DEBUG_NAME);
            }
        }
        AudioRequest::Watch { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }
    return Ok(None);
}

// TODO(fxb/55719): Remove when clients are ported to watch.
async fn process_request_2(
    context: RequestContext<AudioSettings, AudioWatch2Responder>,
    req: AudioRequest,
) -> Result<Option<AudioRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        AudioRequest::Watch2 { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }
    return Ok(None);
}
