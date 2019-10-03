// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::audio::StreamVolumeControl,
    crate::input::monitor_mic_mute,
    crate::registry::base::{Command, Notifier, State},
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::*,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_async as fasync,
    futures::StreamExt,
    parking_lot::RwLock,
    std::collections::HashMap,
    std::sync::Arc,
};

// TODO(go/fxb/35983): Load default values from a config.
const DEFAULT_MIC_MUTE: bool = false;
pub const DEFAULT_VOLUME_LEVEL: f32 = 0.5;
pub const DEFAULT_VOLUME_MUTED: bool = false;

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
    stream_map: &HashMap<AudioStreamType, StreamVolumeControl>,
) -> [AudioStream; 5] {
    let mut streams: [AudioStream; 5] = DEFAULT_STREAMS;
    for i in 0..streams.len() {
        if let Some(volume_control) = stream_map.get(&streams[i].stream_type) {
            streams[i] = volume_control.stored_stream.clone();
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
    let mic_mute_state = Arc::<RwLock<bool>>::new(RwLock::new(DEFAULT_MIC_MUTE));

    // TODO(go/fxb/35878): Add persistent storage.
    // TODO(go/fxb/35983): Load default values from a config.
    let mut stream_volume_controls = HashMap::new();

    let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();

    monitor_mic_mute(service_context_handle.clone(), input_tx);

    let mic_mute_state_clone = mic_mute_state.clone();
    let notifier_lock_clone = notifier_lock.clone();
    fasync::spawn(async move {
        while let Some(event) = input_rx.next().await {
            if let Some(mic_mute) = event.mic_mute {
                if *mic_mute_state_clone.read() != mic_mute {
                    *mic_mute_state_clone.write() = mic_mute;
                    if let Some(notifier) = (*notifier_lock_clone.read()).clone() {
                        notifier.unbounded_send(SettingType::Audio).unwrap();
                    }
                }
            }
        }
    });

    fasync::spawn(async move {
        // Connect to the audio core service.
        let audio_service = service_context_handle
            .read()
            .connect::<fidl_fuchsia_media::AudioCoreMarker>()
            .expect("connected to audio core");

        for stream in DEFAULT_STREAMS.iter() {
            stream_volume_controls.insert(
                stream.stream_type.clone(),
                StreamVolumeControl::create(&audio_service, stream.clone()),
            );
        }

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
                            update_volume_stream(&volume, &mut stream_volume_controls).await;

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
                                        update_volume_stream(&streams, &mut stream_volume_controls)
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
                                    streams: get_streams_array_from_map(&stream_volume_controls),
                                    input: AudioInputInfo { mic_mute: *mic_mute_state.read() },
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
    stored_volume_controls: &mut HashMap<AudioStreamType, StreamVolumeControl>,
) {
    for stream in new_streams {
        if let Some(volume_control) = stored_volume_controls.get_mut(&stream.stream_type) {
            volume_control.set_volume(stream.clone()).await;
        }
    }
}
