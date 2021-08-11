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
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::Duration,
    futures::future::Fuse,
    futures::{self, FutureExt, StreamExt},
    serde::{Deserialize, Serialize},
    std::sync::Arc,
};

/// The amount of time in milliseconds to wait for a camera device to be detected.
pub const CAMERA_WATCHER_TIMEOUT: i64 = 3000;

/// Builder to simplify construction of fidl_fuchsia_ui_input::MediaButtonsEvent.
/// # Example usage:
/// ```
/// MediaButtonsEventBuilder::new().set_volume(1).set_mic_mute(true).build();
/// ```
pub(crate) struct MediaButtonsEventBuilder {
    volume: i8,
    mic_mute: bool,
    pause: bool,
    camera_disable: bool,
}

#[allow(dead_code)]
impl MediaButtonsEventBuilder {
    pub(crate) fn new() -> Self {
        // Create with defaults.
        Self { volume: 0, mic_mute: false, pause: false, camera_disable: false }
    }

    pub(crate) fn build(self) -> MediaButtonsEvent {
        MediaButtonsEvent {
            volume: Some(self.volume),
            mic_mute: Some(self.mic_mute),
            pause: Some(self.pause),
            camera_disable: Some(self.camera_disable),
            ..MediaButtonsEvent::EMPTY
        }
    }

    pub(crate) fn set_volume(mut self, volume: i8) -> Self {
        self.volume = volume;
        self
    }

    pub(crate) fn set_mic_mute(mut self, mic_mute: bool) -> Self {
        self.mic_mute = mic_mute;
        self
    }

    pub(crate) fn set_pause(mut self, pause: bool) -> Self {
        self.pause = pause;
        self
    }

    pub(crate) fn set_camera_disable(mut self, camera_disable: bool) -> Self {
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
pub(crate) async fn monitor_media_buttons(
    service_context_handle: Arc<ServiceContext>,
    sender: futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>,
) -> Result<(), Error> {
    let presenter_service =
        service_context_handle.connect::<DeviceListenerRegistryMarker>().await?;
    let (client_end, mut stream) = create_request_stream::<MediaButtonsListenerMarker>()
        .expect("failed to create request stream for media buttons listener");

    if let Err(error) = call_async!(presenter_service => register_listener(client_end)).await {
        fx_log_err!("Registering media button listener with presenter service failed {:?}", error);
        return Err(format_err!("presenter service not ready"));
    }

    fasync::Task::spawn(async move {
        while let Some(Ok(media_request)) = stream.next().await {
            // Support future expansion of FIDL
            #[allow(clippy::single_match)]
            #[allow(unreachable_patterns)]
            match media_request {
                MediaButtonsListenerRequest::OnEvent { event, responder } => {
                    sender
                        .unbounded_send(event)
                        .expect("Media buttons sender failed to send event");
                    // Acknowledge the event.
                    responder
                        .send()
                        .unwrap_or_else(|_| fx_log_err!("Failed to ack media buttons event"));
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

    // Sets a timer and watches for changes from the camera api. If the first response is empty,
    // continue to watch for an update to the devices. If we receive a nonempty response,
    // we extract the id and return. If the timeout is reached, then it is assumed to be an error.
    let timer =
        fasync::Timer::new(Duration::from_millis(CAMERA_WATCHER_TIMEOUT).after_now()).fuse();
    let camera_ids = call_async!(camera_watcher_proxy => watch_devices()).fuse();

    // Used to add the second watch call if the first comes back with empty devices.
    let unfulfilled_future = Fuse::terminated();

    futures::pin_mut!(timer, camera_ids, unfulfilled_future);
    loop {
        futures::select! {
            ids_result = camera_ids => {
                let ids = ids_result?;
                if ids.is_empty() {
                    // The camera list might not be initialized yet, make another watch call and
                    // keep waiting.
                    let next_camera_ids = call_async!(camera_watcher_proxy => watch_devices()).fuse();
                    unfulfilled_future.set(next_camera_ids);
                } else {
                    // Nonempty response, extract id.
                    return extract_cam_id(ids);
                }
            }
            ids_result_second = unfulfilled_future => {
                let ids = ids_result_second?;
                return extract_cam_id(ids);
            }
            _ = timer => {
                return Err(format_err!("Could not find a camera"));
            }
        }
    }
}

/// Extract the camera id from the list of ids. Assumes there is only one camera.
fn extract_cam_id(ids: Vec<WatchDevicesEvent>) -> Result<u64, Error> {
    let first_cam = ids.first();
    if let Some(WatchDevicesEvent::Existing(id)) | Some(WatchDevicesEvent::Added(id)) = first_cam {
        Ok(*id)
    } else {
        Err(format_err!("Could not find a camera"))
    }
}

/// Establishes a connection to the fuchsia.camera3.Device api by watching
/// the camera id and using it to connect to the device.
pub(crate) async fn connect_to_camera(
    service_context_handle: Arc<ServiceContext>,
) -> Result<Camera3DeviceProxy, Error> {
    // Connect to the camera device watcher to get camera ids. This will
    // be used to connect to the camera.
    let camera_watcher_proxy = connect_to_camera_watcher(service_context_handle).await?;
    let camera_id = get_camera_id(&camera_watcher_proxy).await?;

    // Connect to the camera device with the found id.
    let (camera_proxy, device_server) = create_proxy::<DeviceMarker>()?;
    if call!(camera_watcher_proxy => connect_to_device(camera_id, device_server)).is_err() {
        return Err(format_err!("Could not connect to fuchsia.camera3.DeviceWatcher device"));
    }
    Ok(camera_proxy)
}
