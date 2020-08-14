// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::handler::base::{SettingHandlerResult, State};
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use async_trait::async_trait;
use {
    crate::input::{InputMonitor, InputMonitorHandle, InputType},
    crate::switchboard::base::{
        ControllerStateResult, InputInfo, Microphone, SettingRequest, SettingResponse,
    },
    futures::lock::Mutex,
    std::sync::Arc,
};

impl DeviceStorageCompatible for InputInfo {
    const KEY: &'static str = "input_info";

    fn default_value() -> Self {
        InputInfo { microphone: Microphone { muted: false } }
    }
}

type InputControllerInnerHandle = Arc<Mutex<InputControllerInner>>;

/// Inner struct for the InputController.
///
/// Allows the controller to use a lock on its contents.
struct InputControllerInner {
    /// Client to communicate with persistent store and notify on.
    client: ClientProxy<InputInfo>,

    /// Handles and reports the media button states.
    input_monitor: InputMonitorHandle<InputInfo>,

    /// Local tracking of the hardware mic state.
    hardware_mic_muted: bool,

    /// Local tracking of the software mic state.
    software_mic_muted: bool,
}

impl InputControllerInner {
    /// Gets the input state.
    async fn get_info(&mut self) -> Result<InputInfo, ControllerError> {
        let mut input_info = self.client.read().await;
        let mut input_monitor = self.input_monitor.lock().await;
        input_monitor.ensure_monitor().await;
        self.hardware_mic_muted = input_monitor.get_mute_state();
        let muted = self.hardware_mic_muted || self.software_mic_muted;

        input_info.microphone = Microphone { muted };
        Ok(input_info)
    }

    /// Restores the input state.
    // TODO(fxbug.dev/57917): After config is implemented, this should return a ControllerStateResult.
    async fn restore(&mut self) {
        // Get hardware state.
        let mut input_monitor = self.input_monitor.lock().await;
        input_monitor.ensure_monitor().await;
        self.hardware_mic_muted = input_monitor.get_mute_state();

        // Get software state.
        self.software_mic_muted = self.client.read().await.microphone.muted;
    }

    /// Sets the software mic state to [muted].
    async fn set_mic_mute(&mut self, muted: bool) -> SettingHandlerResult {
        let mut input_info = self.client.read().await;
        input_info.microphone.muted = muted;

        let mut input_monitor = self.input_monitor.lock().await;
        input_monitor.ensure_monitor().await;

        self.hardware_mic_muted = input_monitor.get_mute_state();
        self.software_mic_muted = muted;

        // Store the newly set value.
        write(&self.client, input_info, false).await.into_handler_result()
    }
}

pub struct InputController {
    /// Handle so that a lock can be used in the Handle trait implementation.
    inner: InputControllerInnerHandle,
}

#[async_trait]
impl data_controller::Create<InputInfo> for InputController {
    /// Creates the controller.
    async fn create(client: ClientProxy<InputInfo>) -> Result<Self, ControllerError> {
        Ok(Self {
            inner: Arc::new(Mutex::new(InputControllerInner {
                client: client.clone(),
                input_monitor: InputMonitor::create(client.clone(), vec![InputType::Microphone]),
                hardware_mic_muted: false,
                software_mic_muted: false,
            })),
        })
    }
}

#[async_trait]
impl controller::Handle for InputController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        #[allow(unreachable_patterns)]
        match request {
            SettingRequest::Restore => {
                // Get hardware state.
                // TODO(fxbug.dev/57917): After config is implemented, handle the error here.
                self.inner.lock().await.restore().await;
                Some(Ok(None))
            }
            SettingRequest::SetMicMute(muted) => {
                Some((*self.inner.lock().await).set_mic_mute(muted).await)
            }
            SettingRequest::Get => Some(
                (*self.inner.lock().await)
                    .get_info()
                    .await
                    .map(|info| Some(SettingResponse::Input(info))),
            ),
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
