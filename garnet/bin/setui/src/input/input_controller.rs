// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::config::default_settings::DefaultSetting;
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, ControllerStateResult, IntoHandlerResult, SettingHandlerResult,
    State,
};
use crate::input::common::connect_to_camera;
use crate::input::input_device_configuration::InputConfiguration;
use crate::input::types::{
    DeviceState, DeviceStateSource, InputDevice, InputDeviceType, InputInfo, InputInfoSources,
    InputState, Microphone,
};
use crate::input::ButtonType;

use async_trait::async_trait;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use serde::{Deserialize, Serialize};
use std::sync::Arc;

pub(crate) const DEFAULT_CAMERA_NAME: &str = "camera";
pub(crate) const DEFAULT_MIC_NAME: &str = "microphone";

impl DeviceStorageCompatible for InputInfoSources {
    const KEY: &'static str = "input_info";

    fn default_value() -> Self {
        InputInfoSources { input_device_state: InputState::new() }
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(&value)
            .unwrap_or_else(|_| Self::from(InputInfoSourcesV2::deserialize_from(&value)))
    }
}

impl From<InputInfoSourcesV2> for InputInfoSources {
    fn from(v2: InputInfoSourcesV2) -> Self {
        let mut input_state = v2.input_device_state;

        // Convert the old states into an input device.
        input_state.set_source_state(
            InputDeviceType::MICROPHONE,
            DEFAULT_MIC_NAME.to_string(),
            DeviceStateSource::HARDWARE,
            if v2.hw_microphone.muted { DeviceState::MUTED } else { DeviceState::AVAILABLE },
        );
        input_state.set_source_state(
            InputDeviceType::MICROPHONE,
            DEFAULT_MIC_NAME.to_string(),
            DeviceStateSource::SOFTWARE,
            if v2.sw_microphone.muted { DeviceState::MUTED } else { DeviceState::AVAILABLE },
        );

        InputInfoSources { input_device_state: input_state }
    }
}

impl From<InputInfoSources> for SettingInfo {
    fn from(info: InputInfoSources) -> SettingInfo {
        SettingInfo::Input(info.into())
    }
}

impl From<InputInfoSources> for InputInfo {
    fn from(info: InputInfoSources) -> InputInfo {
        InputInfo { input_device_state: info.input_device_state }
    }
}

impl From<InputInfo> for SettingInfo {
    fn from(info: InputInfo) -> SettingInfo {
        SettingInfo::Input(info)
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct InputInfoSourcesV2 {
    hw_microphone: Microphone,
    sw_microphone: Microphone,
    input_device_state: InputState,
}

impl DeviceStorageCompatible for InputInfoSourcesV2 {
    const KEY: &'static str = "input_info_sources_v2";

    fn default_value() -> Self {
        InputInfoSourcesV2 {
            hw_microphone: Microphone { muted: false },
            sw_microphone: Microphone { muted: false },
            input_device_state: InputState::new(),
        }
    }

    fn deserialize_from(value: &str) -> Self {
        Self::extract(&value)
            .unwrap_or_else(|_| Self::from(InputInfoSourcesV1::deserialize_from(&value)))
    }
}

impl From<InputInfoSourcesV1> for InputInfoSourcesV2 {
    fn from(v1: InputInfoSourcesV1) -> Self {
        InputInfoSourcesV2 {
            hw_microphone: v1.hw_microphone,
            sw_microphone: v1.sw_microphone,
            input_device_state: InputState::new(),
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct InputInfoSourcesV1 {
    pub hw_microphone: Microphone,
    pub sw_microphone: Microphone,
}

impl InputInfoSourcesV1 {
    const fn new(hw_microphone: Microphone, sw_microphone: Microphone) -> InputInfoSourcesV1 {
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
    client: ClientProxy,

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
    async fn get_stored_info(&self) -> InputInfo {
        let mut input_info = self.client.read_setting::<InputInfo>().await;
        if input_info.input_device_state.is_empty() {
            input_info.input_device_state = self.input_device_config.clone().into();
        }
        input_info
    }

    /// Gets the input state.
    async fn get_info(&mut self) -> Result<InputInfo, ControllerError> {
        Ok(InputInfo { input_device_state: self.input_device_state.clone() })
    }

    /// Restores the input state.
    // TODO(fxbug.dev/57917): After config is implemented, this should return a ControllerStateResult.
    async fn restore(&mut self) {
        let input_info = self.get_stored_info().await;
        self.input_device_state = input_info.input_device_state;
    }

    /// Sets the software mic state to `muted`.
    // TODO(fxb/65686): remove when FIDL is changed.
    async fn set_sw_mic_mute(&mut self, muted: bool) -> SettingHandlerResult {
        let mut input_info = self.get_stored_info().await;
        input_info.input_device_state.set_source_state(
            InputDeviceType::MICROPHONE,
            DEFAULT_MIC_NAME.to_string(),
            DeviceStateSource::SOFTWARE,
            if muted { DeviceState::MUTED } else { DeviceState::AVAILABLE },
        );

        // Store the newly set value.
        self.client.write_setting(input_info.into(), true).await.into_handler_result()
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

    async fn set_sw_camera_mute(&mut self, disabled: bool, name: String) -> SettingHandlerResult {
        let mut input_info = self.get_stored_info().await;
        input_info.input_device_state.set_source_state(
            InputDeviceType::CAMERA,
            name.clone(),
            DeviceStateSource::SOFTWARE,
            if disabled { DeviceState::MUTED } else { DeviceState::AVAILABLE },
        );

        self.input_device_state.set_source_state(
            InputDeviceType::CAMERA,
            name.clone(),
            DeviceStateSource::SOFTWARE,
            if disabled { DeviceState::MUTED } else { DeviceState::AVAILABLE },
        );
        self.client.write_setting(input_info.into(), true).await.into_handler_result()
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

        let mut hw_state = hw_state_res.map_err(|err| {
            ControllerError::UnexpectedError(
                format!("Could not fetch current hw mute state: {:?}", err).into(),
            )
        })?;

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
        self.client.write_setting(input_info.into(), true).await.into_handler_result()
    }

    /// Sets state for the given input devices.
    async fn set_input_states(
        &mut self,
        input_devices: Vec<InputDevice>,
        source: DeviceStateSource,
    ) -> SettingHandlerResult {
        let mut input_info = self.get_stored_info().await;
        let device_types = input_info.input_device_state.device_types();

        let cam_state = self.get_cam_sw_state().ok();
        // TODO(fxbug.dev/69639): Design a more generalized approach to detecting changes in
        // specific areas of input state and pushing necessary changes to other components.

        for input_device in input_devices.iter() {
            if !device_types.contains(&input_device.device_type) {
                return Err(ControllerError::UnsupportedError(SettingType::Input));
            }
            input_info.input_device_state.insert_device(input_device.clone(), source);
            self.input_device_state.insert_device(input_device.clone(), source);
        }

        // If the device has a camera, it should successfully get the sw state, and
        // push the state if it has changed. If the device does not have a camera,
        // it should be None both here and above, and thus not detect a change.
        let modified_cam_state = self.get_cam_sw_state().ok();
        if cam_state != modified_cam_state {
            if let Some(state) = modified_cam_state {
                self.push_cam_sw_state(state).await?;
            }
        }

        // Store the newly set value.
        self.client.write_setting(input_info.into(), true).await.into_handler_result()
    }

    /// Pulls the current software state of the camera from the device state.
    fn get_cam_sw_state(&self) -> Result<DeviceState, ControllerError> {
        self.input_device_state
            .get_source_state(
                InputDeviceType::CAMERA,
                DEFAULT_CAMERA_NAME.to_string(),
                DeviceStateSource::SOFTWARE,
            )
            .map_err(|_| {
                ControllerError::UnexpectedError("Could not find camera software state".into())
            })
    }

    /// Forwards the given software state to the camera3 api. Will first establish
    /// a connection to the camera3.DeviceWatcher api. This function should only be called
    /// when there is a camera included in the config. The config is used to populate the
    /// stored input_info, so the input_info's input_device_state can be checked whether its
    /// device_types contains Camera prior to calling this function.
    async fn push_cam_sw_state(&mut self, cam_state: DeviceState) -> Result<(), ControllerError> {
        let is_muted = cam_state.has_state(DeviceState::MUTED);

        // Start up a connection to the camera device watcher and connect to the
        // camera proxy using the id that is returned. The connection will drop out
        // of scope after the mute state is sent.
        let camera_proxy =
            connect_to_camera(self.client.get_service_context()).await.map_err(|e| {
                ControllerError::UnexpectedError(
                    format!("Could not connect to camera device: {:?}", e).into(),
                )
            })?;

        camera_proxy.set_software_mute_state(is_muted).await.map_err(|e| {
            fx_log_err!("Failed to push cam state: {:#?}", e);
            ControllerError::ExternalFailure(
                SettingType::Input,
                "fuchsia.camera3.Device".into(),
                "SetSoftwareMuteState".into(),
            )
        })
    }
}

pub struct InputController {
    /// Handle so that a lock can be used in the Handle trait implementation.
    inner: InputControllerInnerHandle,
}

impl DeviceStorageAccess for InputController {
    const STORAGE_KEYS: &'static [&'static str] = &[InputInfoSources::KEY];
}

impl InputController {
    /// Alternate constructor that allows specifying a configuration.
    pub(crate) async fn create_with_config(
        client: ClientProxy,
        input_device_config: InputConfiguration,
    ) -> Result<Self, ControllerError> {
        Ok(Self {
            inner: Arc::new(Mutex::new(InputControllerInner {
                client,
                input_device_state: InputState::new(),
                input_device_config,
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
impl data_controller::Create for InputController {
    /// Creates the controller.
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        if let Ok(Some(config)) = DefaultSetting::<InputConfiguration, &str>::new(
            None,
            "/config/data/input_device_config.json",
        )
        .load_default_value_and_report(client.messenger())
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
            // TODO(fxb/65686): remove when FIDL is changed.
            Request::SetMicMute(muted) => {
                Some(self.inner.lock().await.set_sw_mic_mute(muted).await)
            }
            Request::Get => Some(
                self.inner.lock().await.get_info().await.map(|info| Some(SettingInfo::Input(info))),
            ),
            Request::OnCameraSWState(is_muted) => {
                let old_state = match self
                    .inner
                    .lock()
                    .await
                    .get_stored_info()
                    .await
                    .input_device_state
                    .get_source_state(
                        InputDeviceType::CAMERA,
                        DEFAULT_CAMERA_NAME.to_string(),
                        DeviceStateSource::SOFTWARE,
                    )
                    .map_err(|_| {
                        ControllerError::UnexpectedError(
                            "Could not find camera software state".into(),
                        )
                    }) {
                    Ok(state) => state,
                    Err(e) => return Some(Err(e)),
                };
                if old_state.has_state(DeviceState::MUTED) != is_muted {
                    return Some(
                        self.inner
                            .lock()
                            .await
                            .set_sw_camera_mute(is_muted, DEFAULT_CAMERA_NAME.to_string())
                            .await,
                    );
                }
                None
            }
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
    let mut expected_input_state = InputState::new();
    expected_input_state.set_source_state(
        InputDeviceType::MICROPHONE,
        DEFAULT_MIC_NAME.to_string(),
        DeviceStateSource::SOFTWARE,
        DeviceState::MUTED,
    );
    expected_input_state.set_source_state(
        InputDeviceType::MICROPHONE,
        DEFAULT_MIC_NAME.to_string(),
        DeviceStateSource::HARDWARE,
        DeviceState::AVAILABLE,
    );
    assert_eq!(current.input_device_state, expected_input_state);
}

#[test]
fn test_input_migration_v1_to_v2() {
    const MUTED_MIC: Microphone = Microphone { muted: true };
    let mut v1 = InputInfoSourcesV1::default_value();
    v1.sw_microphone = MUTED_MIC;

    let serialized_v1 = v1.serialize_to();
    let v2 = InputInfoSourcesV2::deserialize_from(&serialized_v1);

    assert_eq!(v2.hw_microphone, Microphone { muted: false });
    assert_eq!(v2.sw_microphone, MUTED_MIC);
    assert_eq!(v2.input_device_state, InputState::new());
}

#[test]
fn test_input_migration_v2_to_current() {
    const DEFAULT_CAMERA_NAME: &str = "camera";
    const MUTED_MIC: Microphone = Microphone { muted: true };
    let mut v2 = InputInfoSourcesV2::default_value();
    v2.input_device_state.set_source_state(
        InputDeviceType::CAMERA,
        DEFAULT_CAMERA_NAME.to_string(),
        DeviceStateSource::SOFTWARE,
        DeviceState::AVAILABLE,
    );
    v2.input_device_state.set_source_state(
        InputDeviceType::CAMERA,
        DEFAULT_CAMERA_NAME.to_string(),
        DeviceStateSource::HARDWARE,
        DeviceState::MUTED,
    );
    v2.sw_microphone = MUTED_MIC;

    let serialized_v2 = v2.serialize_to();
    let current = InputInfoSources::deserialize_from(&serialized_v2);
    let mut expected_input_state = InputState::new();

    expected_input_state.set_source_state(
        InputDeviceType::MICROPHONE,
        DEFAULT_MIC_NAME.to_string(),
        DeviceStateSource::SOFTWARE,
        DeviceState::MUTED,
    );
    expected_input_state.set_source_state(
        InputDeviceType::MICROPHONE,
        DEFAULT_MIC_NAME.to_string(),
        DeviceStateSource::HARDWARE,
        DeviceState::AVAILABLE,
    );
    expected_input_state.set_source_state(
        InputDeviceType::CAMERA,
        DEFAULT_CAMERA_NAME.to_string(),
        DeviceStateSource::SOFTWARE,
        DeviceState::AVAILABLE,
    );
    expected_input_state.set_source_state(
        InputDeviceType::CAMERA,
        DEFAULT_CAMERA_NAME.to_string(),
        DeviceStateSource::HARDWARE,
        DeviceState::MUTED,
    );

    assert_eq!(current.input_device_state, expected_input_state);
}
