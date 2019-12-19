// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_processor::process_stream, crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::Sender, fidl::endpoints::ServiceMarker,
    fidl_fuchsia_media::AudioRenderUsage, fidl_fuchsia_settings::*, fuchsia_async as fasync,
    futures::future::LocalBoxFuture, futures::prelude::*,
};

impl Sender<AudioSettings> for AudioWatchResponder {
    fn send_response(self, data: AudioSettings) {
        self.send(&mut Ok(data)).log_fidl_response_error(AudioMarker::DEBUG_NAME);
    }
}

impl From<SettingResponse> for AudioSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Audio(info) = response {
            let mut streams = Vec::new();
            for stream in info.streams.iter() {
                streams.push(AudioStreamSettings::from(stream.clone()));
            }

            let mut audio_input = AudioInput::empty();
            audio_input.muted = Some(info.input.mic_mute);

            let mut audio_settings = AudioSettings::empty();
            audio_settings.streams = Some(streams);
            audio_settings.input = Some(audio_input);
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

pub fn spawn_audio_fidl_handler(switchboard_handle: SwitchboardHandle, stream: AudioRequestStream) {
    process_stream::<AudioMarker, AudioSettings, AudioWatchResponder>(
        stream,
        switchboard_handle,
        SettingType::Audio,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<AudioRequest>, failure::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        AudioRequest::Set { settings, responder } => {
                            if let Some(request) = to_request(settings) {
                                set_volume(context.switchboard.clone(), request, responder).await
                            } else {
                                responder
                                    .send(&mut Err(Error::Unsupported))
                                    .log_fidl_response_error(AudioMarker::DEBUG_NAME);
                            }
                        }
                        AudioRequest::Watch { responder } => context.watch(responder).await,
                        _ => {
                            return Ok(Some(req));
                        }
                    }

                    return Ok(None);
                }
                .boxed_local()
            },
        ),
    );
}

async fn set_volume(
    switchboard: SwitchboardHandle,
    request: SettingRequest,
    responder: AudioSetResponder,
) {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    if switchboard.lock().await.request(SettingType::Audio, request, response_tx).is_ok() {
        fasync::spawn(async move {
            // Return success if we get a Ok result from the
            // switchboard.
            if let Ok(Ok(_)) = response_rx.await {
                responder.send(&mut Ok(())).log_fidl_response_error(AudioMarker::DEBUG_NAME);
            } else {
                responder
                    .send(&mut Err(fidl_fuchsia_settings::Error::Failed))
                    .log_fidl_response_error(AudioMarker::DEBUG_NAME);
            }
        });
    } else {
        responder
            .send(&mut Err(fidl_fuchsia_settings::Error::Failed))
            .log_fidl_response_error(AudioMarker::DEBUG_NAME);
    }
}
