// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::audio::{default_audio_info, play_sound, StreamVolumeControl},
    crate::input::monitor_mic_mute,
    crate::registry::base::{Command, Notifier, State},
    crate::registry::device_storage::DeviceStorage,
    crate::service_context::ServiceContextHandle,
    crate::switchboard::base::*,
    anyhow::{Context as _, Error},
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_media::{
        AudioRenderUsage,
        Usage::RenderUsage,
        UsageReporterMarker,
        UsageState::{Ducked, Muted},
        UsageWatcherRequest::OnStateChanged,
    },
    fidl_fuchsia_media_sounds::{PlayerMarker, PlayerProxy},
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::lock::Mutex,
    futures::StreamExt,
    parking_lot::RwLock,
    std::collections::{HashMap, HashSet},
    std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
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
    service_context_handle: ServiceContextHandle,
    storage: Arc<Mutex<DeviceStorage<AudioInfo>>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (audio_handler_tx, mut audio_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let default_audio_settings = default_audio_info();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));
    let mic_mute_state =
        Arc::<RwLock<bool>>::new(RwLock::new(default_audio_settings.input.mic_mute));
    let volume_button_event = Arc::<Mutex<i8>>::new(Mutex::new(0));

    let priority_stream_playing = Arc::new(AtomicBool::new(false));
    let input_service_connected = Arc::<RwLock<bool>>::new(RwLock::new(false));
    let audio_service_connected = Arc::<RwLock<bool>>::new(RwLock::new(false));

    let mut sound_player_connection = None;
    let mut stream_volume_controls = HashMap::new();

    let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();

    let input_service_connected_clone = input_service_connected.clone();
    let service_context_handle_clone = service_context_handle.clone();
    let input_tx_clone = input_tx.clone();

    fasync::spawn(async move {
        *input_service_connected_clone.write() =
            monitor_mic_mute(service_context_handle_clone, input_tx_clone).await.is_ok();
    });

    let mic_mute_state_clone = mic_mute_state.clone();
    let volume_button_event_clone = volume_button_event.clone();
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
            if let Some(volume) = event.volume {
                *volume_button_event_clone.lock().await = volume;
                if let Some(notifier) = (*notifier_lock_clone.read()).clone() {
                    notifier.unbounded_send(SettingType::Audio).unwrap();
                }
            }
        }
    });

    let priority_stream_playing_clone = priority_stream_playing.clone();
    let service_context_handle_clone = service_context_handle.clone();
    fasync::spawn(async move {
        match watch_background_usage(&service_context_handle_clone, priority_stream_playing_clone)
            .await
        {
            Ok(_) => {}
            Err(err) => fx_log_err!("Failed while watching background usage: {}", err),
        };
    });

    let input_service_connected_clone = input_service_connected.clone();
    let volume_button_event_clone = volume_button_event.clone();
    let service_context_handle_clone = service_context_handle.clone();
    fasync::spawn(async move {
        check_and_bind_volume_controls(
            audio_service_connected.clone(),
            service_context_handle_clone,
            &mut stream_volume_controls,
        )
        .await
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
                                sound_player_connection =
                                    connect_to_sound_player(&service_context_handle).await;
                            }

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

                            let last_media_user_volume =
                                volume_on_stream(AudioStreamType::Media, &stream_volume_controls);
                            update_volume_stream(&volume, &mut stream_volume_controls).await;
                            let new_media_user_volume =
                                volume_on_stream(AudioStreamType::Media, &stream_volume_controls);
                            let volume_up_max_pressed = new_media_user_volume == Some(1.0)
                                && *volume_button_event_clone.lock().await == 1;
                            if last_media_user_volume != new_media_user_volume
                                || volume_up_max_pressed
                            {
                                play_media_volume_sound(
                                    sound_player_connection.clone(),
                                    priority_stream_playing.clone(),
                                    new_media_user_volume,
                                    &mut sound_player_added_files,
                                )
                                .await;
                            }

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
                                if !*input_service_connected_clone.read() {
                                    let connected = monitor_mic_mute(
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
                                }))))
                                .ok();
                        }
                        _ => {
                            responder
                                .send(Err(Error::new(SwitchboardError::UnimplementedRequest {
                                    setting_type: SettingType::Audio,
                                    request: request,
                                })))
                                .ok();
                        }
                    }
                }
            }
        }
    });
    audio_handler_tx
}

// We should not play earcons over a higher priority stream. In order to figure out whether there
// is a more high-priority stream playing, we watch the BACKGROUND audio usage. If it is muted or
// ducked, we should not play the earcons.
// TODO (fxb/44381): Remove this when it is no longer necessary to check the background usage.
async fn watch_background_usage(
    service_context_handle: &ServiceContextHandle,
    priority_stream_playing: Arc<AtomicBool>,
) -> Result<(), Error> {
    let usage_reporter_proxy =
        service_context_handle.lock().await.connect::<UsageReporterMarker>()?;

    // Create channel for usage reporter watch results.
    let (watcher_client, mut watcher_requests) = create_request_stream()?;

    // Watch for changes in usage.
    usage_reporter_proxy.watch(&mut RenderUsage(AudioRenderUsage::Background), watcher_client)?;

    // Handle changes in the usage, update state.
    while let Some(event) = watcher_requests.next().await {
        match event {
            Ok(OnStateChanged { usage: _usage, state, responder }) => {
                responder.send()?;
                priority_stream_playing.store(
                    match state {
                        Muted(_) | Ducked(_) => true,
                        _ => false,
                    },
                    Ordering::SeqCst,
                );
            }
            Err(e) => {
                return Err(Error::new(SwitchboardError::ExternalFailure {
                    setting_type: SettingType::Audio,
                    dependency: "UseageReporterProxy".to_string(),
                    request: "watch".to_string(),
                    error: Error::new(e),
                }));
            }
        }
    }
    Ok(())
}

// Retrieve the user volume on the specified stream, given the currently stored streams.
fn volume_on_stream(
    stream_type: AudioStreamType,
    stored_streams: &HashMap<AudioStreamType, StreamVolumeControl>,
) -> Option<f32> {
    match stored_streams.get(&stream_type) {
        Some(stream) => Some(stream.stored_stream.user_volume_level),
        None => {
            fx_log_err!("Could not find {:?} stream", stream_type);
            return None;
        }
    }
}

// Play the earcons sound given the changed volume streams.
async fn play_media_volume_sound(
    sound_player_connection: Option<PlayerProxy>,
    priority_stream_playing: Arc<AtomicBool>,
    volume_level: Option<f32>,
    mut sound_player_added_files: &mut HashSet<&str>,
) {
    if let (Some(sound_player_proxy), Some(volume_level)) =
        (sound_player_connection.clone(), volume_level)
    {
        if priority_stream_playing.load(Ordering::SeqCst) {
            fx_log_info!("Detected a stream already playing, not playing earcons sound");
            return;
        }

        if volume_level >= 1.0 {
            play_sound(&sound_player_proxy, VOLUME_MAX_FILE_PATH, 0, &mut sound_player_added_files)
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

// Establish a connection to the sound player and return the proxy representing the service.
async fn connect_to_sound_player(
    service_context_handle: &ServiceContextHandle,
) -> Option<PlayerProxy> {
    match service_context_handle
        .lock()
        .await
        .connect::<PlayerMarker>()
        .context("Connecting to fuchsia.media.sounds.Player")
    {
        Ok(result) => Some(result),
        Err(e) => {
            fx_log_err!("Failed to connect to fuchsia.media.sounds.Player: {}", e);
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
async fn check_and_bind_volume_controls(
    service_connected: Arc<RwLock<bool>>,
    service_context_handle: ServiceContextHandle,
    stream_volume_controls: &mut HashMap<AudioStreamType, StreamVolumeControl>,
) -> Result<(), Error> {
    if *service_connected.read() {
        return Ok(());
    }

    let service_result =
        service_context_handle.lock().await.connect::<fidl_fuchsia_media::AudioCoreMarker>();

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
