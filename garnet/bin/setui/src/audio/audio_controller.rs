// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::audio::{default_audio_info, StreamVolumeControl},
    crate::input::monitor_media_buttons,
    crate::registry::base::{Command, Context, Notifier, SettingHandler, State},
    crate::registry::device_storage::DeviceStorageFactory,
    crate::service_context::ServiceContextHandle,
    crate::switchboard::base::*,
    anyhow::Error,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::StreamExt,
    parking_lot::RwLock,
    std::collections::HashMap,
    std::sync::Arc,
};

fn get_streams_array_from_map(
    stream_map: &HashMap<AudioStreamType, StreamVolumeControl>,
) -> [AudioStream; 5] {
    let mut streams: [AudioStream; 5] = default_audio_info().streams;
    for i in 0..streams.len() {
        if let Some(volume_control) = stream_map.get(&streams[i].stream_type) {
            streams[i] = volume_control.stored_stream.clone();
        }
    }
    streams
}

/// Controller that handles commands for SettingType::Audio.
/// TODO(go/fxb/35988): Hook up the presentation service to listen for the mic mute state.
pub fn spawn_audio_controller<T: DeviceStorageFactory + Send + Sync + 'static>(
    context: &Context<T>,
) -> SettingHandler {
    let service_context_handle = context.environment.service_context_handle.clone();
    let storage_factory_handle = context.environment.storage_factory_handle.clone();
    let (audio_handler_tx, mut audio_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let default_audio_settings = default_audio_info();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));
    let mic_mute_state =
        Arc::<RwLock<bool>>::new(RwLock::new(default_audio_settings.input.mic_mute));
    let input_service_connected = Arc::<RwLock<bool>>::new(RwLock::new(false));
    let audio_service_connected = Arc::<RwLock<bool>>::new(RwLock::new(false));

    let mut stream_volume_controls = HashMap::new();

    let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();

    let input_service_connected_clone = input_service_connected.clone();
    let service_context_handle_clone = service_context_handle.clone();
    let input_tx_clone = input_tx.clone();

    fasync::spawn(async move {
        *input_service_connected_clone.write() =
            monitor_media_buttons(service_context_handle_clone, input_tx_clone).await.is_ok();
    });

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
        fx_log_err!("[audio_controller] exited input event loop");
    });

    let input_service_connected_clone = input_service_connected.clone();
    let service_context_handle_clone = service_context_handle.clone();
    fasync::spawn(async move {
        check_and_bind_volume_controls(
            audio_service_connected.clone(),
            service_context_handle_clone,
            &mut stream_volume_controls,
        )
        .await
        .ok();

        let storage = storage_factory_handle.lock().await.get_store::<AudioInfo>();
        let mut changed_streams = None;

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
                    let mut stored_value = storage.lock().await.get().await;

                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::Restore => {
                            let stored_streams = stored_value.streams.iter().cloned().collect();
                            update_volume_stream(&stored_streams, &mut stream_volume_controls)
                                .await;
                        }
                        SettingRequest::SetVolume(volume) => {
                            // TODO(fxb/48736): remove temporary logging.
                            fx_log_info!("[audio_controller] received SettingRequest::SetVolume");

                            // TODO(fxb/48736): remove temporary logging.
                            fx_log_info!("[audio_controller] binding volume controls");
                            if check_and_bind_volume_controls(
                                audio_service_connected.clone(),
                                service_context_handle.clone(),
                                &mut stream_volume_controls,
                            )
                            .await
                            .is_err()
                            {
                                continue;
                            };
                            update_volume_stream(&volume, &mut stream_volume_controls).await;
                            changed_streams = Some(volume);
                            stored_value.streams =
                                get_streams_array_from_map(&stream_volume_controls);

                            let storage_handle_clone = storage.clone();
                            fasync::spawn(async move {
                                // TODO(fxb/48736): remove temporary logging.
                                fx_log_info!("[audio_controller] storing audio values");
                                let mut storage_lock = storage_handle_clone.lock().await;
                                storage_lock.write(&stored_value, false).await.unwrap_or_else(
                                    move |e| {
                                        fx_log_err!("failed storing audio, {}", e);
                                    },
                                );
                            });
                            let _ = responder.send(Ok(None)).ok();
                            if let Some(notifier) = (*notifier_lock.read()).clone() {
                                notifier.unbounded_send(SettingType::Audio).unwrap();
                            }
                        }
                        SettingRequest::Get => {
                            {
                                if !*input_service_connected_clone.read() {
                                    let connected = monitor_media_buttons(
                                        service_context_handle.clone(),
                                        input_tx.clone(),
                                    )
                                    .await
                                    .is_ok();

                                    *input_service_connected_clone.write() = connected;
                                }
                            }

                            check_and_bind_volume_controls(
                                audio_service_connected.clone(),
                                service_context_handle.clone(),
                                &mut stream_volume_controls,
                            )
                            .await
                            .ok();
                            let _ = responder
                                .send(Ok(Some(SettingResponse::Audio(AudioInfo {
                                    streams: stored_value.streams,
                                    input: AudioInputInfo { mic_mute: *mic_mute_state.read() },
                                    changed_streams: changed_streams.clone(),
                                }))))
                                .ok();
                        }
                        _ => {
                            responder
                                .send(Err(SwitchboardError::UnimplementedRequest {
                                    setting_type: SettingType::Audio,
                                    request: request,
                                }))
                                .ok();
                        }
                    }
                }
            }
        }
        fx_log_err!("[audio_controller] exited service event loop");
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

// Checks to see if |service_connected| contains true. If it is not, then
// connect to the audio core service. If the service connects successfully,
// set |service_connected| to true and create a StreamVolumeControl for each
// stream type.
async fn check_and_bind_volume_controls(
    service_connected: Arc<RwLock<bool>>,
    service_context_handle: ServiceContextHandle,
    stream_volume_controls: &mut HashMap<AudioStreamType, StreamVolumeControl>,
) -> Result<(), Error> {
    if *service_connected.read() {
        return Ok(());
    }

    let service_result =
        service_context_handle.lock().await.connect::<fidl_fuchsia_media::AudioCoreMarker>().await;

    let audio_service = match service_result {
        Ok(service) => {
            *service_connected.write() = true;
            service
        }
        Err(err) => {
            fx_log_err!("failed to connect to audio core, {}", err);
            return Err(err);
        }
    };

    for stream in default_audio_info().streams.iter() {
        stream_volume_controls.insert(
            stream.stream_type.clone(),
            StreamVolumeControl::create(&audio_service, stream.clone()),
        );
    }
    Ok(())
}
