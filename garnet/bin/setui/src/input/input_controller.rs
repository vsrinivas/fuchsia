// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::handler::base::{SettingHandlerResult, State};
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use crate::input::ButtonType;
use crate::switchboard::base::{
    Camera, ControllerStateResult, InputInfo, InputInfoSources, Microphone, SettingRequest,
    SettingResponse,
};
use async_trait::async_trait;
use futures::lock::Mutex;
use std::sync::Arc;

impl DeviceStorageCompatible for InputInfoSources {
    const KEY: &'static str = "input_info";

    fn default_value() -> Self {
        InputInfoSources {
            hw_microphone: Microphone { muted: false },
            sw_microphone: Microphone { muted: false },
            hw_camera: Camera { disabled: false },
        }
    }
}

type InputControllerInnerHandle = Arc<Mutex<InputControllerInner>>;

/// Inner struct for the InputController.
///
/// Allows the controller to use a lock on its contents.
struct InputControllerInner {
    /// Client to communicate with persistent store and notify on.
    client: ClientProxy<InputInfoSources>,

    /// Local tracking of the hardware mic state.
    hardware_mic_muted: bool,

    /// Local tracking of the software mic state.
    software_mic_muted: bool,

    /// Local tracking of the hardware camera state.
    hardware_camera_disabled: bool,
}

impl InputControllerInner {
    /// Gets the input state.
    async fn get_info(&mut self) -> Result<InputInfo, ControllerError> {
        let mic_muted = self.hardware_mic_muted || self.software_mic_muted;
        let camera_disabled = self.hardware_camera_disabled;
        Ok(InputInfo {
            microphone: Microphone { muted: mic_muted },
            camera: Camera { disabled: camera_disabled },
        })
    }

    /// Restores the input state.
    // TODO(fxbug.dev/57917): After config is implemented, this should return a ControllerStateResult.
    async fn restore(&mut self) {
        let input_info = self.client.read().await;
        self.hardware_mic_muted = input_info.hw_microphone.muted;
        self.software_mic_muted = input_info.sw_microphone.muted;
        self.hardware_camera_disabled = input_info.hw_camera.disabled;
    }

    /// Sets the software mic state to `muted`.
    async fn set_sw_mic_mute(&mut self, muted: bool) -> SettingHandlerResult {
        let mut input_info = self.client.read().await;
        input_info.sw_microphone.muted = muted;

        self.software_mic_muted = muted;

        // Store the newly set value.
        write(&self.client, input_info, false).await.into_handler_result()
    }

    /// Sets the hardware mic state to `muted`.
    async fn set_hw_mic_mute(&mut self, muted: bool) -> SettingHandlerResult {
        let mut input_info = self.client.read().await;
        input_info.hw_microphone.muted = muted;

        self.hardware_mic_muted = muted;

        // Store the newly set value.
        write(&self.client, input_info, false).await.into_handler_result()
    }

    /// Sets the hardware camera disable to `disabled`.
    async fn set_hw_camera_disable(&mut self, disabled: bool) -> SettingHandlerResult {
        let mut input_info = self.client.read().await;
        input_info.hw_camera.disabled = disabled;

        self.hardware_camera_disabled = disabled;

        // Store the newly set value.
        write(&self.client, input_info, false).await.into_handler_result()
    }
}

pub struct InputController {
    /// Handle so that a lock can be used in the Handle trait implementation.
    inner: InputControllerInnerHandle,
}

#[async_trait]
impl data_controller::Create<InputInfoSources> for InputController {
    /// Creates the controller.
    async fn create(client: ClientProxy<InputInfoSources>) -> Result<Self, ControllerError> {
        Ok(Self {
            inner: Arc::new(Mutex::new(InputControllerInner {
                client: client.clone(),
                hardware_mic_muted: false,
                software_mic_muted: false,
                hardware_camera_disabled: false,
            })),
        })
    }
}

#[async_trait]
impl controller::Handle for InputController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        match request {
            SettingRequest::Restore => {
                // Get hardware state.
                // TODO(fxbug.dev/57917): After config is implemented, handle the error here.
                self.inner.lock().await.restore().await;
                Some(Ok(None))
            }
            SettingRequest::SetMicMute(muted) => {
                Some(self.inner.lock().await.set_sw_mic_mute(muted).await)
            }
            SettingRequest::Get => Some(
                self.inner
                    .lock()
                    .await
                    .get_info()
                    .await
                    .map(|info| Some(SettingResponse::Input(info))),
            ),
            SettingRequest::OnButton(ButtonType::MicrophoneMute(state)) => {
                Some(self.inner.lock().await.set_hw_mic_mute(state).await)
            }
            SettingRequest::OnButton(ButtonType::CameraDisable(state)) => {
                Some(self.inner.lock().await.set_hw_camera_disable(state).await)
            }
            _ => None,
        }
    }

    async fn change_state(&mut self, state: State) -> Option<ControllerStateResult> {
        match state {
            State::Startup => {
                // TODO(fxbug.dev/57917): After config is implemented, handle the error here.
                self.inner.lock().await.restore().await;
                Some(Ok(()))
            }
            _ => None,
        }
    }
}
