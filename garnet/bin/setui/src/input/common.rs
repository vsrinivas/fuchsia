// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::call,
    crate::service_context::ServiceContextHandle,
    anyhow::{format_err, Error},
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fidl_fuchsia_ui_policy::{
        DeviceListenerRegistryMarker, MediaButtonsListenerMarker, MediaButtonsListenerRequest,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::StreamExt,
    serde::{Deserialize, Serialize},
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
            ..MediaButtonsEvent::empty()
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

/// Method for listening to media button changes. Changes will be reported back
/// on the supplied sender.
pub async fn monitor_media_buttons(
    service_context_handle: ServiceContextHandle,
    sender: futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>,
) -> Result<(), Error> {
    let presenter_service =
        service_context_handle.lock().await.connect::<DeviceListenerRegistryMarker>().await?;
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

    Ok(())
}
