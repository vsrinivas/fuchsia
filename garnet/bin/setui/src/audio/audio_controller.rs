// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::audio::{default_audio_info, play_sound, StreamVolumeControl},
    crate::input::monitor_mic_mute,
    crate::registry::base::{Command, Notifier, State},
    crate::registry::device_storage::DeviceStorage,
    crate::service_context::ServiceContext,
    crate::switchboard::base::*,
    anyhow::{Context as _, Error},
    fidl_fuchsia_media_sounds::{PlayerMarker, PlayerProxy},
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_async as fasync, fuchsia_component as component,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::StreamExt,
    parking_lot::RwLock,
    std::collections::{HashMap, HashSet},
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

const VOLUME_MAX_FILE_PATH: &str = "volume-max.wav";
const VOLUME_CHANGED_FILE_PATH: &str = "volume-changed.wav";

/// Controller that handles commands for SettingType::Audio.
/// TODO(go/fxb/35988): Hook up the presentation service to listen for the mic mute state.
pub fn spawn_audio_controller(
    service_context_handle: Arc<RwLock<ServiceContext>>,
    storage: Arc<Mutex<DeviceStorage<AudioInfo>>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (audio_handler_tx, mut audio_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let default_audio_settings = default_audio_info();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));
    let mic_mute_state =
        Arc::<RwLock<bool>>::new(RwLock::new(default_audio_settings.input.mic_mute));

    let input_service_connected = Arc::<RwLock<bool>>::new(RwLock::new(false));
    let audio_service_connected = Arc::<RwLock<bool>>::new(RwLock::new(false));

    let mut sound_player_connection = None;
    let mut stream_volume_controls = HashMap::new();

    let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();

    {
        let mut input_service_connected_lock = input_service_connected.write();
        *input_service_connected_lock =
            monitor_mic_mute(service_context_handle.clone(), input_tx.clone()).is_ok();
    }

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

    let input_service_connected_clone = input_service_connected.clone();
    fasync::spawn(async move {
        check_and_bind_volume_controls(
            audio_service_connected.clone(),
            service_context_handle.clone(),
            &mut stream_volume_controls,
        )
        .ok();

        // Load data from persistent storage.
        let mut stored_value: AudioInfo;
        {
            let mut storage_lock = storage.lock().await;
            stored_value = storage_lock.get().await;
        }
        let mut sound_player_added_files: HashSet<&str> = HashSet::new();

        let stored_streams = stored_value.streams.iter().cloned().collect();
        update_volume_stream(&stored_streams, &mut stream_volume_controls).await;

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
                            // Connect to the SoundPlayer the first time a volume event occurs.
                            if sound_player_connection.is_none() {
                                sound_player_connection = connect_to_sound_player();
                            }

                            if check_and_bind_volume_controls(
                                audio_service_connected.clone(),
                                service_context_handle.clone(),
                                &mut stream_volume_controls,
                            )
                            .is_err()
                            {
                                continue;
                            };

                            // TODO(fxb/43075): Test that the sounds are played on volume change and
                            // not played on mute change once the earcons is moved into its own
                            // service.
                            play_media_volume_sound(
                                sound_player_connection.clone(),
                                &volume,
                                &stream_volume_controls,
                                &mut sound_player_added_files,
                            )
                            .await;

                            update_volume_stream(&volume, &mut stream_volume_controls).await;
                            stored_value.streams =
                                get_streams_array_from_map(&stream_volume_controls);
                            persist_audio_info(stored_value.clone(), storage.clone()).await;

                            let _ = responder.send(Ok(None)).ok();
                            if let Some(notifier) = (*notifier_lock.read()).clone() {
                                notifier.unbounded_send(SettingType::Audio).unwrap();
                            }
                        }
                        SettingRequest::Get => {
                            {
                                let mut input_service_connected_lock =
                                    input_service_connected_clone.write();

                                if !*input_service_connected_lock {
                                    *input_service_connected_lock = monitor_mic_mute(
                                        service_context_handle.clone(),
                                        input_tx.clone(),
                                    )
                                    .is_ok();
                                }
                            }

                            check_and_bind_volume_controls(
                                audio_service_connected.clone(),
                                service_context_handle.clone(),
                                &mut stream_volume_controls,
                            )
                            .ok();

                            let _ = responder
                                .send(Ok(Some(SettingResponse::Audio(AudioInfo {
                                    streams: stored_value.streams,
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

// Play the earcons sound given the changed volume streams.
async fn play_media_volume_sound(
    sound_player_connection: Option<PlayerProxy>,
    volume: &Vec<AudioStream>,
    stored_streams: &HashMap<AudioStreamType, StreamVolumeControl>,
    mut sound_player_added_files: &mut HashSet<&str>,
) {
    if let Some(sound_player_proxy) = sound_player_connection.clone() {
        let media_stream = volume.iter().find(|x| x.stream_type == AudioStreamType::Media);
        if let Some(stream) = media_stream {
            let media_volume_stream = match stored_streams.get(&AudioStreamType::Media) {
                Some(stream) => stream,
                None => {
                    fx_log_err!("Could not find media stream");
                    return;
                }
            };
            let last_media_user_volume = media_volume_stream.stored_stream.user_volume_level;

            // If volume didn't change, don't play the sound.
            if stream.user_volume_level == last_media_user_volume {
                return;
            };
            let volume_level = stream.user_volume_level;
            if volume_level >= 1.0 {
                play_sound(
                    &sound_player_proxy,
                    VOLUME_MAX_FILE_PATH,
                    0,
                    &mut sound_player_added_files,
                )
                .await
                .ok();
            } else if volume_level > 0.0 {
                play_sound(
                    &sound_player_proxy,
                    VOLUME_CHANGED_FILE_PATH,
                    1,
                    &mut sound_player_added_files,
                )
                .await
                .ok();
            }
        }
    }
}

// Establish a connection to the sound player and return the proxy representing the service.
fn connect_to_sound_player() -> Option<PlayerProxy> {
    match component::client::connect_to_service::<PlayerMarker>()
        .context("Connecting to fuchsia.media.sounds.Player")
    {
        Ok(result) => Some(result),
        Err(e) => {
            fx_log_err!("[earcons] Failed to connect to fuchsia.media.sounds.Player: {}", e);
            None
        }
    }
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

async fn persist_audio_info(info: AudioInfo, storage: Arc<Mutex<DeviceStorage<AudioInfo>>>) {
    fasync::spawn(async move {
        let mut storage_lock = storage.lock().await;
        let write_request = storage_lock.write(&info, false).await;
        write_request.unwrap_or_else(move |e| {
            fx_log_err!("failed storing audio, {}", e);
        });
    });
}

// Checks to see if |service_connected| contains true. If it is not, then
// connect to the audio core service. If the service connects successfully,
// set |service_connected| to true and create a StreamVolumeControl for each
// stream type.
fn check_and_bind_volume_controls(
    service_connected: Arc<RwLock<bool>>,
    service_context_handle: Arc<RwLock<ServiceContext>>,
    stream_volume_controls: &mut HashMap<AudioStreamType, StreamVolumeControl>,
) -> Result<(), Error> {
    let mut service_connected_lock = service_connected.write();
    if *service_connected_lock {
        return Ok(());
    }

    let service_result =
        service_context_handle.read().connect::<fidl_fuchsia_media::AudioCoreMarker>();

    let audio_service = match service_result {
        Ok(service) => {
            *service_connected_lock = true;
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
