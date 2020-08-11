// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::call,
    crate::handler::device_storage::DeviceStorageCompatible,
    crate::handler::setting_handler::persist::ClientProxy,
    crate::internal::event::Publisher,
    crate::service_context::{ExternalServiceProxy, ServiceContextHandle},
    anyhow::{format_err, Error},
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fidl_fuchsia_ui_policy::{
        DeviceListenerRegistryMarker, DeviceListenerRegistryProxy, MediaButtonsListenerMarker,
        MediaButtonsListenerRequest,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::StreamExt,
    serde::{Deserialize, Serialize},
    std::collections::HashSet,
    std::iter::FromIterator,
    std::sync::Arc,
};

pub type InputMonitorHandle<T> = Arc<Mutex<InputMonitor<T>>>;
pub type MediaButtonEventSender = futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>;

/// Monitors the media buttons for changes and maintains the state.
pub struct InputMonitor<T: Send + Sync + DeviceStorageCompatible + 'static> {
    /// Provides service context and notifier.
    client: ClientProxy<T>,

    /// Whether the media button service is connected.
    service_connected: bool,

    /// Sender for the media button events.
    input_tx: MediaButtonEventSender,

    /// The media button types this monitor supports.
    input_types: HashSet<InputType>,

    /// The state of the mic mute.
    mic_mute_state: bool,

    /// The most recent volume button event.
    volume_button_event: i8,
}

/// The possible types of input to monitor.
#[derive(Eq, PartialEq, Debug, Clone, Copy, Hash, Serialize, Deserialize)]
pub enum InputType {
    Microphone,
    VolumeButtons,
}

impl<T> InputMonitor<T>
where
    T: DeviceStorageCompatible + Send + Sync + 'static,
{
    pub fn create(client: ClientProxy<T>, input_types: Vec<InputType>) -> InputMonitorHandle<T> {
        let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
        let monitor_handle = Arc::new(Mutex::new(Self {
            client,
            service_connected: false,
            input_tx,
            input_types: HashSet::from_iter(input_types.iter().cloned()),
            mic_mute_state: false,
            volume_button_event: 0,
        }));

        let monitor_handle_clone = monitor_handle.clone();
        fasync::Task::spawn(async move {
            monitor_handle_clone.lock().await.ensure_monitor().await;

            while let Some(event) = input_rx.next().await {
                monitor_handle_clone.lock().await.process_event(event).await;
            }
        })
        .detach();

        monitor_handle
    }

    /// The current mic mute state.
    pub fn get_mute_state(&self) -> bool {
        self.mic_mute_state
    }

    /// Process the raw event and use it to set the state.
    async fn process_event(&mut self, event: MediaButtonsEvent) {
        if let (Some(mic_mute), true) =
            (event.mic_mute, self.input_types.contains(&InputType::Microphone))
        {
            if self.mic_mute_state != mic_mute {
                self.mic_mute_state = mic_mute;
            }
        }
        if let (Some(volume), true) =
            (event.volume, self.input_types.contains(&InputType::VolumeButtons))
        {
            self.volume_button_event = volume;
        }

        self.client.notify().await;
    }

    /// Ensure that the service is monitoring the media buttons.
    pub async fn ensure_monitor(&mut self) {
        if self.service_connected {
            return;
        }

        self.service_connected = monitor_media_buttons(
            self.client.get_service_context().await.clone(),
            self.input_tx.clone(),
        )
        .await
        .is_ok();
    }
}

pub async fn monitor_media_buttons(
    service_context_handle: ServiceContextHandle,
    sender: futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>,
) -> Result<(), Error> {
    let presenter_service =
        service_context_handle.lock().await.connect::<DeviceListenerRegistryMarker>().await?;

    monitor_media_buttons_internal(presenter_service, sender).await
}

pub async fn monitor_media_buttons_using_publisher(
    publisher: Publisher,
    service_context_handle: ServiceContextHandle,
    sender: futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>,
) -> Result<(), Error> {
    let presenter_service = service_context_handle
        .lock()
        .await
        .connect_with_publisher::<DeviceListenerRegistryMarker>(publisher)
        .await?;
    monitor_media_buttons_internal(presenter_service, sender).await
}

/// Method for listening to media button changes. Changes will be reported back
/// on the supplied sender.
async fn monitor_media_buttons_internal(
    presenter_service: ExternalServiceProxy<DeviceListenerRegistryProxy>,
    sender: futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>,
) -> Result<(), Error> {
    let (client_end, mut stream) = create_request_stream::<MediaButtonsListenerMarker>().unwrap();

    if call!(presenter_service => register_media_buttons_listener(client_end)).is_err() {
        fx_log_err!("Registering media button listener with presenter service failed.");
        return Err(format_err!("presenter service not ready"));
    }

    fasync::Task::spawn(async move {
        while let Some(Ok(media_request)) = stream.next().await {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match media_request {
                MediaButtonsListenerRequest::OnMediaButtonsEvent { event, control_handle: _ } => {
                    sender.unbounded_send(event).ok();
                }
                _ => {}
            }
        }
    })
    .detach();

    return Ok(());
}
