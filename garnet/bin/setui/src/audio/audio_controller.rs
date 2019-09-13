// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::registry::base::{Command, Notifier, State},
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::*,
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_media_audio::MUTED_GAIN_DB,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::StreamExt,
    parking_lot::RwLock,
    std::collections::HashMap,
    std::sync::Arc,
};

// TODO(go/fxb/35983): Load default values from a config.
const DEFAULT_VOLUME_LEVEL: f32 = 0.5;
const DEFAULT_VOLUME_MUTED: bool = false;

/// Controller that handles commands for SettingType::Audio.
pub fn spawn_audio_controller(
    service_context_handle: Arc<RwLock<ServiceContext>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (audio_handler_tx, mut audio_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    // TODO(go/fxb/35878): Add persistent storage.
    // TODO(go/fxb/35983): Load default values from a config.
    let mut stored_audio_streams = HashMap::new();

    // TODO(go/fxb/35988): Hook up the presentation service to listen for the mic mute state.
    let stored_mic_mute = false;

    let stream_types: [AudioStreamType; 5] = [
        AudioStreamType::Background,
        AudioStreamType::Media,
        AudioStreamType::Interruption,
        AudioStreamType::SystemAgent,
        AudioStreamType::Communication,
    ];

    for stream_type in stream_types.iter() {
        stored_audio_streams
            .insert(stream_type.clone(), create_default_audio_stream(stream_type.clone()));
    }

    fasync::spawn(async move {
        // Connect to the audio core service.
        let audio_service = service_context_handle
            .read()
            .connect::<fidl_fuchsia_media::AudioCoreMarker>()
            .expect("connected to audio core");

        while let Some(command) = audio_handler_rx.next().await {
            match command {
                Command::ChangeState(state) => match state {
                    State::Listen(notifier) => {
                        *notifier_lock.write() = Some(notifier);
                    }
                    State::EndListen => {
                        *notifier_lock.write() = None;
                    }
                },
                Command::HandleRequest(request, responder) =>
                {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::SetVolume(volume) => {
                            for stream in volume {
                                match set_volume(
                                    stream.clone(),
                                    &mut stored_audio_streams,
                                    &audio_service,
                                )
                                .await
                                {
                                    Ok(_) => {}
                                    Err(e) => {
                                        fx_log_err!("failed to set volume: {}", e);
                                    }
                                }
                            }

                            let _ = responder.send(Ok(None)).ok();
                            if let Some(notifier) = (*notifier_lock.read()).clone() {
                                notifier.unbounded_send(SettingType::Audio).unwrap();
                            }
                        }
                        SettingRequest::Get => {
                            let mut streams = Vec::new();
                            for val in stored_audio_streams.values() {
                                streams.push(val.clone());
                            }

                            let _ = responder
                                .send(Ok(Some(SettingResponse::Audio(AudioInfo {
                                    streams: streams,
                                    input: AudioInputInfo { mic_mute: stored_mic_mute },
                                }))))
                                .ok();
                        }
                        _ => panic!("Unexpected command to audio"),
                    }
                }
            }
        }
    });
    audio_handler_tx
}

async fn set_volume(
    stream: AudioStream,
    stored_audio_streams: &mut HashMap<AudioStreamType, AudioStream>,
    audio_service: &fidl_fuchsia_media::AudioCoreProxy,
) -> Result<(), fidl::Error> {
    stored_audio_streams.insert(stream.stream_type, stream.clone());

    if stream.source == AudioSettingSource::User {
        return audio_service.set_render_usage_gain(
            AudioRenderUsage::from(stream.stream_type),
            get_gain_db(stream.user_volume_level, stream.user_volume_muted),
        );
    }

    if stream.source == AudioSettingSource::Default {
        return audio_service.set_render_usage_gain(
            AudioRenderUsage::from(stream.stream_type),
            get_gain_db(DEFAULT_VOLUME_LEVEL, DEFAULT_VOLUME_MUTED),
        );
    }

    return Ok(());
}

// Converts an audio 'level' in the range 0.0 to 1.0 inclusive to a gain in
// db.
// TODO(go/fxb/36148): Use the VolumeControl for the volume curve.
pub fn get_gain_db(level: f32, muted: bool) -> f32 {
    const MIN_LEVEL_GAIN_DB: f32 = -45.0;
    const UNITY_GAIN_DB: f32 = 0.0;

    if muted || level <= 0.0 {
        return MUTED_GAIN_DB;
    }

    if level >= 1.0 {
        return UNITY_GAIN_DB;
    }

    (1.0 - level) * MIN_LEVEL_GAIN_DB
}

pub fn create_default_audio_stream(stream_type: AudioStreamType) -> AudioStream {
    AudioStream {
        stream_type: stream_type,
        source: AudioSettingSource::Default,
        user_volume_level: DEFAULT_VOLUME_LEVEL,
        user_volume_muted: DEFAULT_VOLUME_MUTED,
    }
}
