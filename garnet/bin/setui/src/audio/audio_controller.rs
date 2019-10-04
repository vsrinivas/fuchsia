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
const DEFAULT_MIC_MUTE: bool = false;
const DEFAULT_VOLUME_LEVEL: f32 = 0.5;
const DEFAULT_VOLUME_MUTED: bool = false;

// TODO(go/fxb/35983): Load default values from a config.
pub const DEFAULT_STREAMS: [AudioStream; 5] = [
    create_default_audio_stream(AudioStreamType::Background),
    create_default_audio_stream(AudioStreamType::Media),
    create_default_audio_stream(AudioStreamType::Interruption),
    create_default_audio_stream(AudioStreamType::SystemAgent),
    create_default_audio_stream(AudioStreamType::Communication),
];

// TODO(go/fxb/35983): Load default values from a config.
pub const fn create_default_audio_stream(stream_type: AudioStreamType) -> AudioStream {
    AudioStream {
        stream_type: stream_type,
        source: AudioSettingSource::Default,
        user_volume_level: DEFAULT_VOLUME_LEVEL,
        user_volume_muted: DEFAULT_VOLUME_MUTED,
    }
}

fn get_streams_array_from_map(
    stream_map: &HashMap<AudioStreamType, AudioStream>,
) -> [AudioStream; 5] {
    let mut streams: [AudioStream; 5] = DEFAULT_STREAMS;
    for i in 0..streams.len() {
        if let Some(stored_stream) = stream_map.get(&streams[i].stream_type) {
            streams[i] = stored_stream.clone();
        }
    }
    streams
}

/// Controller that handles commands for SettingType::Audio.
/// TODO(go/fxb/37493): Remove |pair_media_and_system_agent| hack.
pub fn spawn_audio_controller(
    service_context_handle: Arc<RwLock<ServiceContext>>,
    pair_media_and_system_agent: bool,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (audio_handler_tx, mut audio_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    // TODO(go/fxb/35878): Add persistent storage.
    // TODO(go/fxb/35983): Load default values from a config.
    let mut stored_audio_streams = HashMap::new();

    for stream in DEFAULT_STREAMS.iter() {
        stored_audio_streams.insert(stream.stream_type.clone(), stream.clone());
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
                Command::HandleRequest(request, responder) => {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::SetVolume(volume) => {
                            update_volume_stream(
                                &volume,
                                &mut stored_audio_streams,
                                &audio_service,
                            )
                            .await;

                            if pair_media_and_system_agent {
                                // Check to see if |volume| contains the media stream and not
                                // the system agent stream. If so, then set the system agent
                                // stream's volume to the same as the media.
                                let media_stream =
                                    volume.iter().find(|x| x.stream_type == AudioStreamType::Media);
                                let contains_system_stream = volume
                                    .iter()
                                    .find(|x| x.stream_type == AudioStreamType::SystemAgent)
                                    != None;
                                if let Some(media_stream_value) = media_stream {
                                    if !contains_system_stream {
                                        let mut system_stream = media_stream_value.clone();
                                        system_stream.stream_type = AudioStreamType::SystemAgent;
                                        let streams = &vec![system_stream];
                                        update_volume_stream(
                                            &streams,
                                            &mut stored_audio_streams,
                                            &audio_service,
                                        )
                                        .await;
                                    }
                                }
                            }

                            let _ = responder.send(Ok(None)).ok();
                            if let Some(notifier) = (*notifier_lock.read()).clone() {
                                notifier.unbounded_send(SettingType::Audio).unwrap();
                            }
                        }
                        SettingRequest::Get => {
                            let _ = responder
                                .send(Ok(Some(SettingResponse::Audio(AudioInfo {
                                    streams: get_streams_array_from_map(&stored_audio_streams),
                                    input: AudioInputInfo { mic_mute: DEFAULT_MIC_MUTE },
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

// Updates |stored_audio_streams| and then update volume via the AudioCore service.
async fn update_volume_stream(
    new_streams: &Vec<AudioStream>,
    stored_audio_streams: &mut HashMap<AudioStreamType, AudioStream>,
    audio_service: &fidl_fuchsia_media::AudioCoreProxy,
) {
    for stream in new_streams {
        set_volume(stream, audio_service).await.unwrap_or_else(move |e| {
            fx_log_err!("failed updating the audio volume, {}", e);
        });
        stored_audio_streams.insert(stream.stream_type, stream.clone());
    }
}

// Sets the volume via the AudioCore service.
async fn set_volume(
    stream: &AudioStream,
    audio_service: &fidl_fuchsia_media::AudioCoreProxy,
) -> Result<(), fidl::Error> {
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
