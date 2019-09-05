// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::registry::base::{Command, Notifier, State},
    crate::switchboard::base::*,
    fuchsia_async as fasync,
    futures::StreamExt,
    std::collections::HashMap,
    std::sync::{Arc, RwLock},
};

/// Controller that handles commands for SettingType::Audio.
pub fn spawn_audio_controller() -> futures::channel::mpsc::UnboundedSender<Command> {
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
        while let Some(command) = audio_handler_rx.next().await {
            match command {
                Command::ChangeState(state) => match state {
                    State::Listen(notifier) => {
                        *notifier_lock.write().unwrap() = Some(notifier);
                    }
                    State::EndListen => {
                        *notifier_lock.write().unwrap() = None;
                    }
                },
                Command::HandleRequest(request, responder) => {
                    // TODO(go/fxb/35874): Connect to audio core service.
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::SetVolume(volume) => {
                            for stream in volume {
                                stored_audio_streams.insert(stream.stream_type, stream.clone());
                            }

                            let _ = responder.send(Ok(None)).ok();
                            if let Some(notifier) = (*notifier_lock.read().unwrap()).clone() {
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

pub fn create_default_audio_stream(stream_type: AudioStreamType) -> AudioStream {
    AudioStream {
        stream_type: stream_type,
        source: AudioSettingSource::Default,
        user_volume_level: 0.5,
        user_volume_muted: false,
    }
}
