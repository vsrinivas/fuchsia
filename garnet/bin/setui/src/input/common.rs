// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::handler::base::Request,
    crate::service_context::{ExternalServiceProxy, ServiceContext},
    crate::{call, call_async},
    anyhow::{format_err, Error},
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_camera3::{
        DeviceMarker, DeviceProxy as Camera3DeviceProxy, DeviceWatcherMarker,
        DeviceWatcherProxy as Camera3DeviceWatcherProxy, WatchDevicesEvent,
    },
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fidl_fuchsia_ui_policy::{
        DeviceListenerRegistryMarker, MediaButtonsListenerMarker, MediaButtonsListenerRequest,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::StreamExt,
    serde::{Deserialize, Serialize},
    std::sync::Arc,
};

/// Builder to simplify construction of fidl_fuchsia_ui_input::MediaButtonsEvent.
/// # Example usage:
/// ```
/// MediaButtonsEventBuilder::new().set_volume(1).set_mic_mute(true).build();
/// ```
pub struct MediaButtonsEventBuilder {
    volume: i8,
    mic_mute: bool,
    pause: bool,
    camera_disable: bool,
}

#[allow(dead_code)]
impl MediaButtonsEventBuilder {
    pub fn new() -> Self {
        // Create with defaults.
        Self { volume: 0, mic_mute: false, pause: false, camera_disable: false }
    }

    pub fn build(self) -> MediaButtonsEvent {
        MediaButtonsEvent {
            volume: Some(self.volume),
            mic_mute: Some(self.mic_mute),
            pause: Some(self.pause),
            camera_disable: Some(self.camera_disable),
            ..MediaButtonsEvent::EMPTY
        }
    }

    pub fn set_volume(mut self, volume: i8) -> Self {
        self.volume = volume;
        self
    }

    pub fn set_mic_mute(mut self, mic_mute: bool) -> Self {
        self.mic_mute = mic_mute;
        self
    }

    pub fn set_pause(mut self, pause: bool) -> Self {
        self.pause = pause;
        self
    }

    pub fn set_camera_disable(mut self, camera_disable: bool) -> Self {
        self.camera_disable = camera_disable;
        self
    }
}
/// The possible types of input to monitor.
#[derive(Eq, PartialEq, Debug, Clone, Copy, Hash, Serialize, Deserialize)]
pub enum InputType {
    Camera,
    Microphone,
    VolumeButtons,
}

#[derive(PartialEq, Eq, Copy, Clone, Debug)]
pub enum ButtonType {
    MicrophoneMute(bool),
    CameraDisable(bool),
}

#[derive(PartialEq, Eq, Copy, Clone, Debug)]
pub enum VolumeGain {
    /// This is neither up nor down. It is equivalent to no gain.
    Neutral,
    Up,
    Down,
}

impl From<ButtonType> for Request {
    fn from(button_type: ButtonType) -> Self {
        Request::OnButton(button_type)
    }
}

impl From<VolumeGain> for Request {
    fn from(volume_gain: VolumeGain) -> Self {
        Request::OnVolume(volume_gain)
    }
}

/// Method for listening to media button changes. Changes will be reported back
/// on the supplied sender.
pub async fn monitor_media_buttons(
    service_context_handle: Arc<ServiceContext>,
    sender: futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>,
) -> Result<(), Error> {
    let presenter_service =
        service_context_handle.connect::<DeviceListenerRegistryMarker>().await?;
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
                    sender
                        .unbounded_send(event)
                        .expect("Media buttons sender failed to send event");
                }
                _ => {}
            }
        }
    })
    .detach();

    Ok(())
}

/// Connects to the fuchsia.camera3.DeviceWatcher api.
async fn connect_to_camera_watcher(
    service_context_handle: Arc<ServiceContext>,
) -> Result<ExternalServiceProxy<Camera3DeviceWatcherProxy>, Error> {
    service_context_handle.connect::<DeviceWatcherMarker>().await
}

/// Retrieves the id of a camera device given the camera device watcher proxy.
#[allow(dead_code)]
async fn get_camera_id(
    camera_watcher_proxy: &ExternalServiceProxy<Camera3DeviceWatcherProxy>,
) -> Result<u64, Error> {
    // Get a list of id structs containing existing, new, and removed ids.
    let camera_ids = call_async!(camera_watcher_proxy => watch_devices()).await?;

    // TODO(fxbug.dev/66881): support multiple camera devices.
    let first_cam = camera_ids.first();
    if let Some(WatchDevicesEvent::Existing(id)) | Some(WatchDevicesEvent::Added(id)) = first_cam {
        Ok(*id)
    } else {
        Err(format_err!("Could not find a camera"))
    }
}

/// Establishes a connection to the fuchsia.camera3.Device api by watching
/// the camera id and using it to connect to the device.
pub async fn connect_to_camera(
    service_context_handle: Arc<ServiceContext>,
) -> Result<Camera3DeviceProxy, Error> {
    // Connect to the camera device watcher to get camera ids. This will
    // be used to connect to the camera.
    let camera_watcher_proxy = connect_to_camera_watcher(service_context_handle).await?;
    let camera_id = get_camera_id(&camera_watcher_proxy).await?;

    // Connect to the camera device with the found id.
    let (camera_proxy, device_server) = create_proxy::<DeviceMarker>().unwrap();
    if call!(camera_watcher_proxy => connect_to_device(camera_id, device_server)).is_err() {
        return Err(format_err!("Could not connect to fuchsia.camera3.DeviceWatcher device"));
    }
    Ok(camera_proxy)
}
