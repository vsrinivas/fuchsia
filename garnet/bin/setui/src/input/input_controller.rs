// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::config::default_settings::DefaultSetting;
use crate::handler::base::{Request, SettingHandlerResult, State};
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use crate::input::input_device_configuration::InputConfiguration;
use crate::input::types::{
    DeviceState, DeviceStateSource, InputDevice, InputDeviceType, InputInfo, InputInfoSources,
    InputState, Microphone,
};
use crate::input::ButtonType;
use crate::switchboard::base::ControllerStateResult;

use async_trait::async_trait;
use futures::lock::Mutex;
use serde::{Deserialize, Serialize};
use std::sync::Arc;

impl DeviceStorageCompatible for InputInfoSources {
    const KEY: &'static str = "input_info";

    fn default_value() -> Self {
        InputInfoSources {
            hw_microphone: Microphone { muted: false },
            sw_microphone: Microphone { muted: false },
            input_device_state: InputState::new(),
        }
    }

    fn deserialize_from(value: &String) -> Self {
        Self::extract(&value)
            .unwrap_or_else(|_| Self::from(InputInfoSourcesV1::deserialize_from(&value)))
    }
}

impl From<InputInfoSourcesV1> for InputInfoSources {
    fn from(v1: InputInfoSourcesV1) -> Self {
        InputInfoSources {
            hw_microphone: v1.hw_microphone,
            sw_microphone: v1.sw_microphone,
            input_device_state: InputState::new(),
        }
    }
}

impl Into<SettingInfo> for InputInfoSources {
    fn into(self) -> SettingInfo {
        SettingInfo::Input(InputInfo {
            microphone: Microphone { muted: self.hw_microphone.muted || self.sw_microphone.muted },
            input_device_state: self.input_device_state,
        })
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct InputInfoSourcesV1 {
    pub hw_microphone: Microphone,
    pub sw_microphone: Microphone,
}

impl InputInfoSourcesV1 {
    pub const fn new(hw_microphone: Microphone, sw_microphone: Microphone) -> InputInfoSourcesV1 {
        Self { hw_microphone, sw_microphone }
    }
}

impl DeviceStorageCompatible for InputInfoSourcesV1 {
    const KEY: &'static str = "input_info_sources_v1";

    fn default_value() -> Self {
        InputInfoSourcesV1::new(
            Microphone { muted: false }, /*hw_microphone muted*/
            Microphone { muted: false }, /*sw_microphone muted*/
        )
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

    /// Local tracking of the input device states.
    input_device_state: InputState,

    /// Configuration for this device.
    input_device_config: InputConfiguration,
}

// TODO(fxbug.dev/67153): Rename mic "muted" to "disabled".
// This should apply across all SetUI-controlled usages of
// "mute".
impl InputControllerInner {
    // Wrapper around client.read() that fills in the config
    // as the default value if the read value is empty. It may be empty
    // after a migration from a previous InputInfoSources version
    // or on pave.
    async fn get_stored_info(&self) -> InputInfoSources {
        let mut input_info = self.client.read().await;
        if input_info.input_device_state.is_empty() {
            input_info.input_device_state = self.input_device_config.clone().into();
        }
        input_info
    }

    /// Gets the input state.
    async fn get_info(&mut self) -> Result<InputInfo, ControllerError> {
        let mic_muted = self.hardware_mic_muted || self.software_mic_muted;
        Ok(InputInfo {
            microphone: Microphone { muted: mic_muted },
            input_device_state: self.input_device_state.clone(),
        })
    }

    /// Restores the input state.
    // TODO(fxbug.dev/57917): After config is implemented, this should return a ControllerStateResult.
    async fn restore(&mut self) {
        let input_info = self.get_stored_info().await;
        self.hardware_mic_muted = input_info.hw_microphone.muted;
        self.software_mic_muted = input_info.sw_microphone.muted;
        self.input_device_state = input_info.input_device_state;
    }

    /// Sets the software mic state to `muted`.
    async fn set_sw_mic_mute(&mut self, muted: bool) -> SettingHandlerResult {
        let mut input_info = self.get_stored_info().await;
        input_info.sw_microphone.muted = muted;

        self.software_mic_muted = muted;

        // Store the newly set value.
        write(&self.client, input_info, false).await.into_handler_result()
    }

    /// Sets the hardware mic state to `muted`.
    // TODO(fxbug.dev/66881): Send in name of device to set state for, instead
    // of using the device type's to_string.
    async fn set_hw_mic_mute(&mut self, muted: bool) -> SettingHandlerResult {
        self.set_hw_muted_state(InputDeviceType::MICROPHONE, muted).await
    }

    /// Sets the hardware camera disable to `disabled`.
    // TODO(fxbug.dev/66881): Send in name of device to set state for, instead
    // of using the device type's to_string.
    async fn set_hw_camera_disable(&mut self, disabled: bool) -> SettingHandlerResult {
        self.set_hw_muted_state(InputDeviceType::CAMERA, disabled).await
    }

    // A helper for setting the hw state for a |device_type| given the
    // muted |state|.
    async fn set_hw_muted_state(
        &mut self,
        device_type: InputDeviceType,
        muted: bool,
    ) -> SettingHandlerResult {
        let mut input_info = self.get_stored_info().await;

        // Fetch current state.
        let hw_state_res = input_info.input_device_state.get_source_state(
            device_type,
            device_type.to_string(),
            DeviceStateSource::HARDWARE,
        );
        if hw_state_res.is_err() {
            return Err(ControllerError::UnexpectedError(
                "Could not fetch current hw mute state".into(),
            ));
        }
        let mut hw_state = hw_state_res.unwrap().clone();

        // TODO(fxbug.dev/65686): remove once clients are ported over.
        let mut hw_mic = input_info.hw_microphone;
        hw_mic.muted = muted;
        self.hardware_mic_muted = muted;
        input_info.hw_microphone = hw_mic;

        if muted {
            // Unset available and set muted.
            hw_state &= !DeviceState::AVAILABLE;
            hw_state |= DeviceState::MUTED;
        } else {
            // Set available and unset muted.
            hw_state |= DeviceState::AVAILABLE;
            hw_state &= !DeviceState::MUTED;
        }

        // Set the updated state.
        input_info.input_device_state.set_source_state(
            device_type,
            device_type.to_string(),
            DeviceStateSource::HARDWARE,
            hw_state,
        );
        self.input_device_state.set_source_state(
            device_type,
            device_type.to_string(),
            DeviceStateSource::HARDWARE,
            hw_state,
        );

        // Store the newly set value.
        write(&self.client, input_info, false).await.into_handler_result()
    }

    /// Sets state for the given input devices.
    async fn set_input_states(
        &mut self,
        input_devices: Vec<InputDevice>,
        source: DeviceStateSource,
    ) -> SettingHandlerResult {
        let mut input_info = self.get_stored_info().await;
        let device_types = input_info.input_device_state.device_types();
        for input_device in input_devices.iter() {
            if !device_types.contains(&input_device.device_type) {
                return Err(ControllerError::UnsupportedError(SettingType::Input));
            }
            input_info.input_device_state.insert_device(input_device.clone(), source);
            self.input_device_state.insert_device(input_device.clone(), source);
        }
        // Store the newly set value.
        write(&self.client, input_info, false).await.into_handler_result()
    }
}

pub struct InputController {
    /// Handle so that a lock can be used in the Handle trait implementation.
    inner: InputControllerInnerHandle,
}

impl InputController {
    /// Alternate constructor that allows specifying a configuration.
    #[allow(dead_code)]
    pub(crate) async fn create_with_config(
        client: ClientProxy<InputInfoSources>,
        input_device_config: InputConfiguration,
    ) -> Result<Self, ControllerError> {
        Ok(Self {
            inner: Arc::new(Mutex::new(InputControllerInner {
                client: client.clone(),
                hardware_mic_muted: false,
                software_mic_muted: false,
                input_device_state: InputState::new(),
                input_device_config: input_device_config,
            })),
        })
    }

    // Whether the configuration for this device contains a specific |device_type|.
    async fn has_input_device(&self, device_type: InputDeviceType) -> bool {
        let input_device_config_state: InputState =
            self.inner.lock().await.input_device_config.clone().into();
        input_device_config_state.device_types().contains(&device_type)
    }
}

#[async_trait]
impl data_controller::Create<InputInfoSources> for InputController {
    /// Creates the controller.
    async fn create(client: ClientProxy<InputInfoSources>) -> Result<Self, ControllerError> {
        if let Some(config) = DefaultSetting::<InputConfiguration, &str>::new(
            None,
            "/config/data/input_device_config.json",
        )
        .get_default_value()
        {
            InputController::create_with_config(client, config).await
        } else {
            Err(ControllerError::InitFailure("Invalid default input device config".into()))
        }
    }
}

#[async_trait]
impl controller::Handle for InputController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::Restore => {
                // Get hardware state.
                // TODO(fxbug.dev/57917): After config is implemented, handle the error here.
                self.inner.lock().await.restore().await;
                Some(Ok(None))
            }
            Request::SetMicMute(muted) => {
                Some(self.inner.lock().await.set_sw_mic_mute(muted).await)
            }
            Request::Get => Some(
                self.inner.lock().await.get_info().await.map(|info| Some(SettingInfo::Input(info))),
            ),
            Request::OnButton(ButtonType::MicrophoneMute(state)) => {
                if !self.has_input_device(InputDeviceType::MICROPHONE).await {
                    return Some(Ok(None));
                }
                Some(self.inner.lock().await.set_hw_mic_mute(state).await)
            }
            Request::OnButton(ButtonType::CameraDisable(disabled)) => {
                if !self.has_input_device(InputDeviceType::CAMERA).await {
                    return Some(Ok(None));
                }
                Some(self.inner.lock().await.set_hw_camera_disable(disabled).await)
            }
            Request::SetInputStates(input_states) => Some(
                self.inner
                    .lock()
                    .await
                    .set_input_states(input_states, DeviceStateSource::SOFTWARE)
                    .await,
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

#[test]
fn test_input_migration_v1_to_current() {
    const MUTED_MIC: Microphone = Microphone { muted: true };
    let mut v1 = InputInfoSourcesV1::default_value();
    v1.sw_microphone = MUTED_MIC;

    let serialized_v1 = v1.serialize_to();
    let current = InputInfoSources::deserialize_from(&serialized_v1);

    assert_eq!(current.hw_microphone, Microphone { muted: false });
    assert_eq!(current.sw_microphone, MUTED_MIC);
    assert_eq!(current.input_device_state, InputState::new());
}
