// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::audio::{default_audio_info, StreamVolumeControl},
    crate::input::monitor_media_buttons,
    crate::registry::base::{Command, Context, Notifier, SettingHandler, State},
    crate::registry::device_storage::{DeviceStorage, DeviceStorageFactory},
    crate::service_context::ServiceContextHandle,
    crate::switchboard::base::*,
    anyhow::Error,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::future::BoxFuture,
    futures::lock::Mutex,
    futures::StreamExt,
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

type InputMonitorHandle = Arc<Mutex<InputMonitor>>;
type NotifierHandler = Arc<Mutex<Option<Notifier>>>;
type MediaButtonEventSender = futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>;

struct InputMonitor {
    service_context: ServiceContextHandle,
    service_connected: bool,
    input_tx: MediaButtonEventSender,
    notifier: NotifierHandler,
    mic_mute_state: bool,
    volume_button_event: i8,
}

impl InputMonitor {
    fn create(
        service_context: ServiceContextHandle,
        notifier: NotifierHandler,
    ) -> InputMonitorHandle {
        let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
        let default_audio_settings = default_audio_info();
        let monitor_handle = Arc::new(Mutex::new(Self {
            service_context: service_context,
            service_connected: false,
            input_tx: input_tx,
            notifier: notifier,
            mic_mute_state: default_audio_settings.input.mic_mute,
            volume_button_event: 0,
        }));

        let monitor_handle_clone = monitor_handle.clone();
        fasync::spawn(async move {
            monitor_handle_clone.lock().await.ensure_monitor().await;

            while let Some(event) = input_rx.next().await {
                monitor_handle_clone.lock().await.process_event(event).await;
            }
        });

        monitor_handle
    }

    pub fn get_mute_state(&self) -> bool {
        self.mic_mute_state
    }

    async fn process_event(&mut self, event: MediaButtonsEvent) {
        if let Some(mic_mute) = event.mic_mute {
            if self.mic_mute_state != mic_mute {
                self.mic_mute_state = mic_mute;
                if let Some(notifier) = &*self.notifier.lock().await {
                    notifier.unbounded_send(SettingType::Audio).unwrap();
                }
            }
        }
        if let Some(volume) = event.volume {
            self.volume_button_event = volume;
            if let Some(notifier) = &*self.notifier.lock().await {
                notifier.unbounded_send(SettingType::Audio).unwrap();
            }
        }
    }

    async fn ensure_monitor(&mut self) {
        if self.service_connected {
            return;
        }

        self.service_connected =
            monitor_media_buttons(self.service_context.clone(), self.input_tx.clone())
                .await
                .is_ok();
    }
}

type VolumeControllerHandle<T> = Arc<Mutex<VolumeController<T>>>;

pub struct VolumeController<T: DeviceStorageFactory + Send + Sync + 'static> {
    service_context: ServiceContextHandle,
    audio_service_connected: bool,
    stream_volume_controls: HashMap<AudioStreamType, StreamVolumeControl>,
    notifier: NotifierHandler,
    storage_factory: Arc<Mutex<T>>,
    storage: Option<Arc<Mutex<DeviceStorage<AudioInfo>>>>,
    input_monitor: InputMonitorHandle,
    changed_streams: Option<Vec<AudioStream>>,
}

impl<T: DeviceStorageFactory + Send + Sync + 'static> VolumeController<T> {
    fn create(
        service_context: ServiceContextHandle,
        storage_factory: Arc<Mutex<T>>,
        notifier: NotifierHandler,
        input_monitor: InputMonitorHandle,
    ) -> VolumeControllerHandle<T> {
        let handle = Arc::new(Mutex::new(Self {
            stream_volume_controls: HashMap::new(),
            audio_service_connected: false,
            service_context: service_context,
            notifier: notifier,
            storage_factory: storage_factory,
            storage: None,
            input_monitor: input_monitor,
            changed_streams: None,
        }));

        let handle_clone = handle.clone();
        fasync::spawn(async move {
            handle_clone.lock().await.check_and_bind_volume_controls().await.ok();
        });

        handle
    }

    async fn restore(&mut self) {
        let stored_streams =
            self.get_store().await.lock().await.get().await.streams.iter().cloned().collect();
        self.update_volume_stream(&stored_streams).await;
    }

    async fn get_info(&mut self) -> Result<AudioInfo, SwitchboardError> {
        self.input_monitor.lock().await.ensure_monitor().await;
        if self.check_and_bind_volume_controls().await.is_err() {
            // TODO(fxb/49663): This should return an error instead of returning
            // default data.
            return Ok(default_audio_info());
        };

        let mut audio_info = self.get_store().await.lock().await.get().await;
        audio_info.input =
            AudioInputInfo { mic_mute: self.input_monitor.lock().await.get_mute_state() };
        audio_info.changed_streams = self.changed_streams.clone();
        Ok(audio_info)
    }

    async fn set_volume(&mut self, volume: Vec<AudioStream>) -> SettingResponseResult {
        let get_result = self.get_info().await;

        if let Err(e) = get_result {
            return Err(e);
        }

        update_volume_stream(&volume, &mut self.stream_volume_controls).await;
        self.changed_streams = Some(volume);

        let mut stored_value = get_result.unwrap();
        stored_value.streams = get_streams_array_from_map(&self.stream_volume_controls);

        let storage_handle_clone = self.get_store().await.clone();

        let mut storage_lock = storage_handle_clone.lock().await;
        storage_lock.write(&stored_value, false).await.unwrap_or_else(move |e| {
            fx_log_err!("failed storing audio, {}", e);
        });

        if let Some(notifier) = &*self.notifier.lock().await {
            notifier.unbounded_send(SettingType::Audio).unwrap();
        }

        return Ok(None);
    }

    async fn get_store(&mut self) -> Arc<Mutex<DeviceStorage<AudioInfo>>> {
        if let Some(store) = &self.storage {
            return store.clone();
        }

        let store = self.storage_factory.lock().await.get_store::<AudioInfo>();
        self.storage = Some(store.clone());

        store
    }

    async fn update_volume_stream(&mut self, new_streams: &Vec<AudioStream>) {
        for stream in new_streams {
            if let Some(volume_control) = self.stream_volume_controls.get_mut(&stream.stream_type) {
                volume_control.set_volume(stream.clone()).await;
            }
        }
    }

    async fn check_and_bind_volume_controls(&mut self) -> Result<(), Error> {
        if self.audio_service_connected {
            return Ok(());
        }

        let service_result = self
            .service_context
            .lock()
            .await
            .connect::<fidl_fuchsia_media::AudioCoreMarker>()
            .await;

        let audio_service = match service_result {
            Ok(service) => {
                self.audio_service_connected = true;
                service
            }
            Err(err) => {
                fx_log_err!("failed to connect to audio core, {}", err);
                return Err(err);
            }
        };

        for stream in default_audio_info().streams.iter() {
            self.stream_volume_controls.insert(
                stream.stream_type.clone(),
                StreamVolumeControl::create(&audio_service, stream.clone()),
            );
        }

        Ok(())
    }
}

/// Controller that handles commands for SettingType::Audio.
/// TODO(go/fxb/35988): Hook up the presentation service to listen for the mic mute state.
pub fn spawn_audio_controller<T: DeviceStorageFactory + Send + Sync + 'static>(
    context: Context<T>,
) -> BoxFuture<'static, SettingHandler> {
    let service_context_handle = context.environment.service_context_handle.clone();
    let storage_factory_handle = context.environment.storage_factory_handle.clone();
    let (audio_handler_tx, mut audio_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<Mutex<Option<Notifier>>>::new(Mutex::new(None));

    // New additions
    let input_monitor_handle =
        InputMonitor::create(service_context_handle.clone(), notifier_lock.clone());
    let volume_controller_handle = VolumeController::create(
        service_context_handle.clone(),
        storage_factory_handle.clone(),
        notifier_lock.clone(),
        input_monitor_handle.clone(),
    );

    let volume_controller_handle_clone = volume_controller_handle.clone();

    fasync::spawn(async move {
        while let Some(command) = audio_handler_rx.next().await {
            match command {
                Command::ChangeState(state) => match state {
                    State::Listen(notifier) => {
                        *notifier_lock.lock().await = Some(notifier);
                    }
                    State::EndListen => {
                        *notifier_lock.lock().await = None;
                    }
                },
                Command::HandleRequest(request, responder) =>
                {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::Restore => {
                            volume_controller_handle_clone.lock().await.restore().await;
                        }
                        SettingRequest::SetVolume(volume) => {
                            responder
                                .send(
                                    volume_controller_handle_clone
                                        .lock()
                                        .await
                                        .set_volume(volume)
                                        .await,
                                )
                                .ok();
                        }
                        SettingRequest::Get => {
                            let response = match volume_controller_handle_clone
                                .lock()
                                .await
                                .get_info()
                                .await
                            {
                                Ok(info) => Ok(Some(SettingResponse::Audio(info))),
                                Err(e) => Err(e),
                            };

                            responder.send(response).ok();
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
    Box::pin(async move { audio_handler_tx })
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
