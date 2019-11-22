// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::audio::StreamVolumeControl,
    crate::config::default_settings::DefaultSetting,
    crate::input::monitor_mic_mute,
    crate::registry::base::{Command, Notifier, State},
    crate::registry::device_storage::{DeviceStorage, DeviceStorageCompatible},
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::*,
    failure::Error,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
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

pub const DEFAULT_AUDIO_INPUT_INFO: AudioInputInfo = AudioInputInfo { mic_mute: false };

// TODO(go/fxb/35983): Load default values from a config.
pub const DEFAULT_AUDIO_INFO: AudioInfo =
    AudioInfo { streams: DEFAULT_STREAMS, input: DEFAULT_AUDIO_INPUT_INFO };

// TODO(go/fxb/35983): Load default values from a config.
pub const fn create_default_audio_stream(stream_type: AudioStreamType) -> AudioStream {
    AudioStream {
        stream_type: stream_type,
        source: AudioSettingSource::User,
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

impl DeviceStorageCompatible for AudioInfo {
    const KEY: &'static str = "audio_info";

    fn default_setting() -> DefaultSetting<Self> {
        DefaultSetting::new(DEFAULT_AUDIO_INFO)
    }
}

/// Controller that handles commands for SettingType::Audio.
/// TODO(go/fxb/35988): Hook up the presentation service to listen for the mic mute state.
pub fn spawn_audio_controller(
    service_context_handle: Arc<RwLock<ServiceContext>>,
    storage: Arc<Mutex<DeviceStorage<AudioInfo>>>,
    pair_media_and_system_agent: bool,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (audio_handler_tx, mut audio_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));
    let mic_mute_state = Arc::<RwLock<bool>>::new(RwLock::new(DEFAULT_MIC_MUTE));
    let input_service_connected = Arc::<RwLock<bool>>::new(RwLock::new(false));

    let audio_service_connected = Arc::<RwLock<bool>>::new(RwLock::new(false));

    // TODO(go/fxb/35983): Load default values from a config.
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
                            if check_and_bind_volume_controls(
                                audio_service_connected.clone(),
                                service_context_handle.clone(),
                                &mut stream_volume_controls,
                            )
                            .is_err()
                            {
                                continue;
                            };

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

    for stream in DEFAULT_STREAMS.iter() {
        stream_volume_controls.insert(
            stream.stream_type.clone(),
            StreamVolumeControl::create(&audio_service, stream.clone()),
        );
    }
    Ok(())
}
