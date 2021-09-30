// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use crate::handler::device_storage::DeviceStorageConvertible;
use crate::handler::setting_handler::ControllerError;
use crate::input::input_device_configuration::InputConfiguration;

use anyhow::Error;
use bitflags::bitflags;
use fidl_fuchsia_settings::{
    DeviceState as FidlDeviceState, DeviceStateSource as FidlDeviceStateSource,
    DeviceType as FidlDeviceType, InputDevice as FidlInputDevice,
    InputSettings as FidlInputSettings, SourceState as FidlSourceState,
    ToggleStateFlags as FidlToggleFlags,
};
use serde::{Deserialize, Serialize};
use std::borrow::Cow;
use std::collections::{HashMap, HashSet};
use std::fmt;

impl From<SettingInfo> for FidlInputSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::Input(info) = response {
            let mut input_settings = FidlInputSettings::EMPTY;
            let mut input_devices: Vec<FidlInputDevice> = Vec::new();

            info.input_device_state.input_categories.iter().for_each(|(_, category)| {
                category.devices.iter().for_each(|(_, device)| {
                    input_devices.push(device.clone().into());
                })
            });

            input_settings.devices = Some(input_devices);
            input_settings
        } else {
            panic!("Incorrect value sent to input");
        }
    }
}

#[derive(PartialEq, Debug, Clone)]
pub struct InputInfo {
    pub input_device_state: InputState,
}

impl DeviceStorageConvertible for InputInfo {
    type Storable = InputInfoSources;

    fn get_storable(&self) -> Cow<'_, Self::Storable> {
        Cow::Owned(InputInfoSources { input_device_state: self.input_device_state.clone() })
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InputInfoSources {
    pub input_device_state: InputState,
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
// DO NOT USE - this type is deprecated and will be replaced by
// the use of InputDevice.
pub struct Microphone {
    pub muted: bool,
}

#[derive(PartialEq, Debug, Default, Clone, Serialize, Deserialize)]
/// The top-level struct for the input state. It categorizes the input devices
/// by their device type.
pub struct InputState {
    /// The input devices categorized by device type.
    pub input_categories: HashMap<InputDeviceType, InputCategory>,
}

impl InputState {
    pub(crate) fn new() -> Self {
        Self { input_categories: HashMap::<InputDeviceType, InputCategory>::new() }
    }

    /// Insert an InputDevice's state into the internal InputState hierarchy, updating the
    /// state if it already exists or adding the state if it does not.
    pub(crate) fn insert_device(&mut self, input_device: InputDevice, source: DeviceStateSource) {
        self.set_source_state(
            input_device.device_type,
            input_device.name,
            source,
            input_device.state,
        );
    }

    /// Set the `state` for a given device and `source`.
    /// The combination of `device_type` and `device_name`
    /// uniquely identifies the device.
    pub(crate) fn set_source_state(
        &mut self,
        device_type: InputDeviceType,
        device_name: String,
        source: DeviceStateSource,
        state: DeviceState,
    ) {
        // Ensure the category has an entry in the categories map.
        let category = self.input_categories.entry(device_type).or_insert_with(InputCategory::new);

        // Ensure the device has an entry in the devices map.
        let input_device = category
            .devices
            .entry(device_name.clone())
            .or_insert_with(|| InputDevice::new(device_name, device_type));

        // Replace or add the source state in the map. Ignore the old value.
        let _ = input_device.source_states.insert(source, state);
        input_device.compute_input_state();
    }

    /// Retrieve the state of a given device for one of its `source`s.
    /// The combination of `device_type` and `device_name`
    /// uniquely identifies the device. Returns None if it fails to find
    /// the corresponding state for the given arguments.
    pub(crate) fn get_source_state(
        &self,
        device_type: InputDeviceType,
        device_name: String,
        source: DeviceStateSource,
    ) -> Result<DeviceState, Error> {
        return Ok(*self
            .input_categories
            .get(&device_type)
            .ok_or_else(|| {
                ControllerError::UnexpectedError(
                    "Failed to get input category by input type".into(),
                )
            })?
            .devices
            .get(&device_name)
            .ok_or_else(|| {
                ControllerError::UnexpectedError("Failed to get input device by device name".into())
            })?
            .source_states
            .get(&source)
            .ok_or_else(|| {
                ControllerError::UnexpectedError("Failed to get state from source states".into())
            })?);
    }

    /// Retrieve the overall state of a given device.
    /// The combination of `device_type` and `device_name`
    /// uniquely identifies the device. Returns None if it fails to find
    /// the corresponding state for the given arguments.
    pub(crate) fn get_state(
        &self,
        device_type: InputDeviceType,
        device_name: String,
    ) -> Result<DeviceState, Error> {
        return Ok(self
            .input_categories
            .get(&device_type)
            .ok_or_else(|| {
                ControllerError::UnexpectedError(
                    "Failed to get input category by input type".into(),
                )
            })?
            .devices
            .get(&device_name)
            .ok_or_else(|| {
                ControllerError::UnexpectedError("Failed to get input device by device name".into())
            })?
            .state);
    }

    /// Returns true if the state map is empty.
    pub(crate) fn is_empty(&self) -> bool {
        self.input_categories.is_empty()
    }

    /// Returns a set of the `InputDeviceType`s contained in the
    /// state map.
    pub(crate) fn device_types(&self) -> HashSet<InputDeviceType> {
        self.input_categories.keys().cloned().collect()
    }
}

impl From<InputConfiguration> for InputState {
    fn from(config: InputConfiguration) -> Self {
        let mut categories = HashMap::<InputDeviceType, InputCategory>::new();
        let devices = config.devices;

        devices.iter().for_each(|device_config| {
            // Ensure the category has an entry in the categories map.
            let input_device_type = device_config.device_type;
            let category = categories.entry(input_device_type).or_insert_with(InputCategory::new);

            // Ensure the device has an entry in the devices map.
            let device_name = device_config.device_name.clone();
            let device = category
                .devices
                .entry(device_name.clone())
                .or_insert_with(|| InputDevice::new(device_name, input_device_type));

            // Set the entry on the source states map.
            device_config.source_states.iter().for_each(|source_state| {
                let value =
                    DeviceState::from_bits(source_state.state).unwrap_or_else(DeviceState::new);
                // Ignore the old value.
                let _ = device.source_states.insert(source_state.source, value);
            });

            // Recompute the overall state.
            device.compute_input_state();
        });
        InputState { input_categories: categories }
    }
}

#[derive(PartialEq, Debug, Default, Clone, Serialize, Deserialize)]
pub struct InputCategory {
    // Map of input devices in this category, identified by names.
    // It is recommended that the name be the lower-case string
    // representation of the device type if there is only one input
    // device in this category.
    pub devices: HashMap<String, InputDevice>,
}

impl InputCategory {
    fn new() -> Self {
        Self { devices: HashMap::<String, InputDevice>::new() }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct InputDevice {
    /// The unique name within the device type.
    pub name: String,

    /// The type of input device.
    pub device_type: InputDeviceType,

    /// The states for each source.
    pub source_states: HashMap<DeviceStateSource, DeviceState>,

    /// The overall state of the device considering the `source_state`s.
    pub state: DeviceState,
}

impl InputDevice {
    fn new(name: String, device_type: InputDeviceType) -> Self {
        Self {
            name,
            device_type,
            source_states: HashMap::<DeviceStateSource, DeviceState>::new(),
            state: DeviceState::new(),
        }
    }

    fn compute_input_state(&mut self) {
        let mut computed_state = DeviceState::from_bits(0).unwrap();

        for state in self.source_states.values() {
            if state.has_error() {
                computed_state |= DeviceState::ERROR;
            }
            if state.has_state(DeviceState::DISABLED) {
                computed_state |= DeviceState::DISABLED | DeviceState::MUTED;
            }
            if state.has_state(DeviceState::MUTED) {
                computed_state |= DeviceState::MUTED;
            }
            if state.has_state(DeviceState::ACTIVE) {
                computed_state |= DeviceState::ACTIVE | DeviceState::AVAILABLE;
            }
        }

        // If any source has ERROR, DISABLED, MUTED, or ACTIVE, the overall
        // state is that state, in order of precedence. Otherwise, the overall state
        // is AVAILABLE.
        if computed_state.has_error() {
            self.state = DeviceState::ERROR;
        } else if computed_state.has_state(DeviceState::DISABLED) {
            self.state = DeviceState::DISABLED | DeviceState::MUTED;
        } else if computed_state.has_state(DeviceState::MUTED) {
            self.state = DeviceState::MUTED;
        } else if computed_state.has_state(DeviceState::ACTIVE) {
            self.state = DeviceState::ACTIVE | DeviceState::AVAILABLE;
        } else {
            self.state = DeviceState::AVAILABLE;
        }
    }
}

impl From<InputDevice> for FidlInputDevice {
    fn from(device: InputDevice) -> Self {
        let mut result = FidlInputDevice::EMPTY;

        // Convert source states.
        let source_state_map = device.source_states.clone();
        let source_states = Some(
            source_state_map
                .keys()
                .into_iter()
                .map(|source| {
                    let mut source_state = FidlSourceState::EMPTY;
                    source_state.source = Some((*source).into());
                    source_state.state = Some(
                        (*source_state_map.get(&source).expect("Source state map key missing"))
                            .into(),
                    );
                    source_state
                })
                .collect(),
        );

        let mutable_toggle_state: FidlDeviceState =
            DeviceState::default_mutable_toggle_state().into();
        result.device_name = Some(device.name.clone());
        result.device_type = Some(device.device_type.into());
        result.source_states = source_states;
        result.mutable_toggle_state = mutable_toggle_state.toggle_flags;
        result.state = Some(device.state.into());
        result
    }
}

#[derive(PartialEq, Eq, Debug, Copy, Clone, Hash, Serialize, Deserialize)]
pub enum InputDeviceType {
    CAMERA,
    MICROPHONE,
}

/// Instead of defining our own fmt function, an easier way
/// is to derive the 'Display' trait for enums using `enum-display-derive` crate
///
/// <https://docs.rs/enum-display-derive/0.1.0/enum_display_derive/>
///
/// Since addition of this in third_party/rust_crates needs OSRB approval, we
/// define our own function here.
impl fmt::Display for InputDeviceType {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            InputDeviceType::CAMERA => fmt.write_str("camera"),
            InputDeviceType::MICROPHONE => fmt.write_str("microphone"),
        }
    }
}

impl From<FidlDeviceType> for InputDeviceType {
    fn from(device_type: FidlDeviceType) -> Self {
        match device_type {
            FidlDeviceType::Camera => InputDeviceType::CAMERA,
            FidlDeviceType::Microphone => InputDeviceType::MICROPHONE,
        }
    }
}

impl From<InputDeviceType> for FidlDeviceType {
    fn from(device_type: InputDeviceType) -> Self {
        match device_type {
            InputDeviceType::CAMERA => FidlDeviceType::Camera,
            InputDeviceType::MICROPHONE => FidlDeviceType::Microphone,
        }
    }
}

#[derive(PartialEq, Eq, Debug, Copy, Clone, Hash, Serialize, Deserialize)]
pub enum DeviceStateSource {
    HARDWARE,
    SOFTWARE,
}

impl From<FidlDeviceStateSource> for DeviceStateSource {
    fn from(device_state_source: FidlDeviceStateSource) -> Self {
        match device_state_source {
            FidlDeviceStateSource::Hardware => DeviceStateSource::HARDWARE,
            FidlDeviceStateSource::Software => DeviceStateSource::SOFTWARE,
        }
    }
}

impl From<DeviceStateSource> for FidlDeviceStateSource {
    fn from(device_state_source: DeviceStateSource) -> Self {
        match device_state_source {
            DeviceStateSource::HARDWARE => FidlDeviceStateSource::Hardware,
            DeviceStateSource::SOFTWARE => FidlDeviceStateSource::Software,
        }
    }
}

// TODO(fxbug.dev/67156): Add a "BLOCKED" flag to represent policy-driven
// disabling. "DISABLED" also needs to track "MUTED", as it will eventually
// be its replacement. This should also be done for the FIDL.
bitflags! {
    #[derive(Serialize, Deserialize)]
    pub struct DeviceState : u64 {
        const AVAILABLE = 0b00000001;
        const ACTIVE = 0b00000010;
        const MUTED = 0b00000100;
        const DISABLED = 0b00001000;
        const ERROR = 0b00010000;
    }
}

impl Default for DeviceState {
    fn default() -> Self {
        Self::new()
    }
}

impl DeviceState {
    pub(crate) fn new() -> Self {
        // Represents AVAILABLE as the default.
        Self { bits: 1 }
    }

    /// The flags that clients can manipulate by default.
    fn default_mutable_toggle_state() -> Self {
        DeviceState::MUTED | DeviceState::DISABLED
    }

    /// Returns true if the current state contains the given state.
    /// e.g. All the 1 bits in the given `state` are also 1s in the
    /// current state.
    pub(crate) fn has_state(&self, state: DeviceState) -> bool {
        *self & state == state
    }

    /// Returns true if the device's state has an error.
    fn has_error(&self) -> bool {
        let is_err = *self & DeviceState::ERROR == DeviceState::ERROR;
        let incompatible_state = self.has_state(DeviceState::ACTIVE | DeviceState::DISABLED)
            || self.has_state(DeviceState::ACTIVE | DeviceState::MUTED)
            || self.has_state(DeviceState::AVAILABLE | DeviceState::DISABLED)
            || self.has_state(DeviceState::AVAILABLE | DeviceState::MUTED);
        is_err || incompatible_state
    }
}

impl From<FidlDeviceState> for DeviceState {
    fn from(device_state: FidlDeviceState) -> Self {
        if let Some(toggle_flags) = device_state.toggle_flags {
            if let Some(res) = Self::from_bits(toggle_flags.bits()) {
                return res;
            }
        }
        Self::default_mutable_toggle_state()
    }
}

impl From<DeviceState> for FidlDeviceState {
    fn from(device_state: DeviceState) -> Self {
        let mut state = FidlDeviceState::EMPTY;
        state.toggle_flags = FidlToggleFlags::from_bits(device_state.bits);
        state
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::input::input_device_configuration::{
        InputConfiguration, InputDeviceConfiguration, SourceState,
    };

    const DEFAULT_MIC_NAME: &str = "microphone";
    const DEFAULT_CAMERA_NAME: &str = "camera";
    const AVAILABLE_BITS: u64 = 1;
    const MUTED_BITS: u64 = 4;
    const MUTED_DISABLED_BITS: u64 = 12;

    /// Helper to create a `FidlInputDevice`.
    fn create_fidl_input_device(
        device_name: &str,
        device_type: FidlDeviceType,
        sw_bits: u64,
        hw_bits: u64,
        overall_bits: u64,
    ) -> FidlInputDevice {
        FidlInputDevice {
            device_name: Some(device_name.to_string()),
            device_type: Some(device_type),
            source_states: Some(vec![
                FidlSourceState {
                    source: Some(FidlDeviceStateSource::Hardware),
                    state: Some(FidlDeviceState {
                        toggle_flags: FidlToggleFlags::from_bits(hw_bits),
                        ..FidlDeviceState::EMPTY
                    }),
                    ..FidlSourceState::EMPTY
                },
                FidlSourceState {
                    source: Some(FidlDeviceStateSource::Software),
                    state: Some(FidlDeviceState {
                        toggle_flags: FidlToggleFlags::from_bits(sw_bits),
                        ..FidlDeviceState::EMPTY
                    }),
                    ..FidlSourceState::EMPTY
                },
            ]),
            mutable_toggle_state: FidlToggleFlags::from_bits(MUTED_DISABLED_BITS),
            state: Some(FidlDeviceState {
                toggle_flags: FidlToggleFlags::from_bits(overall_bits),
                ..FidlDeviceState::EMPTY
            }),
            ..FidlInputDevice::EMPTY
        }
    }

    /// Helper to create an [`InputDevice`].
    fn create_input_device(
        device_name: &str,
        device_type: InputDeviceType,
        sw_bits: u64,
        hw_bits: u64,
        overall_bits: u64,
    ) -> InputDevice {
        let mut input_device = InputDevice::new(device_name.to_string(), device_type);
        let _ = input_device
            .source_states
            .insert(DeviceStateSource::SOFTWARE, DeviceState::from_bits(sw_bits).unwrap());
        let _ = input_device
            .source_states
            .insert(DeviceStateSource::HARDWARE, DeviceState::from_bits(hw_bits).unwrap());
        input_device.state = DeviceState::from_bits(overall_bits).unwrap();
        input_device
    }

    /// Helper for creating the config for an `InputDevice`.
    fn create_device_config(
        device_name: &str,
        device_type: InputDeviceType,
        sw_state: u64,
        hw_state: u64,
    ) -> InputDeviceConfiguration {
        InputDeviceConfiguration {
            device_name: device_name.to_string(),
            device_type,
            source_states: vec![
                SourceState { source: DeviceStateSource::SOFTWARE, state: sw_state },
                SourceState { source: DeviceStateSource::HARDWARE, state: hw_state },
            ],
            mutable_toggle_state: MUTED_DISABLED_BITS,
        }
    }

    /// Helper for verifying the equality of a `FidlInputDevice`. Cannot directly
    /// compare because the order of the source_states vector may vary.
    fn verify_fidl_input_device_eq(res: FidlInputDevice, expected: FidlInputDevice) {
        assert_eq!(res.device_name, expected.device_name);
        assert_eq!(res.device_type, expected.device_type);
        assert_eq!(res.mutable_toggle_state, expected.mutable_toggle_state);
        assert_eq!(res.state, expected.state);
        let res_source_states = res.source_states.unwrap();
        for source_state in expected.source_states.unwrap() {
            assert!(&res_source_states.contains(&source_state));
        }
    }

    #[test]
    fn test_input_state_manipulation() {
        let mut input_state = InputState::new();

        // Set the source state for each source and device type.
        input_state.set_source_state(
            InputDeviceType::MICROPHONE,
            DEFAULT_MIC_NAME.to_string(),
            DeviceStateSource::SOFTWARE,
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
        input_state.set_source_state(
            InputDeviceType::MICROPHONE,
            DEFAULT_MIC_NAME.to_string(),
            DeviceStateSource::HARDWARE,
            DeviceState::from_bits(MUTED_BITS).unwrap(),
        );
        input_state.set_source_state(
            InputDeviceType::CAMERA,
            DEFAULT_CAMERA_NAME.to_string(),
            DeviceStateSource::SOFTWARE,
            DeviceState::from_bits(MUTED_BITS).unwrap(),
        );
        input_state.set_source_state(
            InputDeviceType::CAMERA,
            DEFAULT_CAMERA_NAME.to_string(),
            DeviceStateSource::HARDWARE,
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );

        // Get the source state for each source and device type.
        assert_eq!(
            input_state
                .get_source_state(
                    InputDeviceType::MICROPHONE,
                    DEFAULT_MIC_NAME.to_string(),
                    DeviceStateSource::SOFTWARE
                )
                .unwrap(),
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
        assert_eq!(
            input_state
                .get_source_state(
                    InputDeviceType::MICROPHONE,
                    DEFAULT_MIC_NAME.to_string(),
                    DeviceStateSource::HARDWARE
                )
                .unwrap(),
            DeviceState::from_bits(MUTED_BITS).unwrap(),
        );
        assert_eq!(
            input_state
                .get_source_state(
                    InputDeviceType::CAMERA,
                    DEFAULT_CAMERA_NAME.to_string(),
                    DeviceStateSource::SOFTWARE
                )
                .unwrap(),
            DeviceState::from_bits(MUTED_BITS).unwrap(),
        );
        assert_eq!(
            input_state
                .get_source_state(
                    InputDeviceType::CAMERA,
                    DEFAULT_CAMERA_NAME.to_string(),
                    DeviceStateSource::HARDWARE
                )
                .unwrap(),
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );

        // Get the overall states for each device.
        assert_eq!(
            input_state
                .get_state(InputDeviceType::MICROPHONE, DEFAULT_MIC_NAME.to_string())
                .unwrap(),
            DeviceState::from_bits(MUTED_BITS).unwrap(),
        );
        assert_eq!(
            input_state
                .get_state(InputDeviceType::CAMERA, DEFAULT_CAMERA_NAME.to_string())
                .unwrap(),
            DeviceState::from_bits(MUTED_BITS).unwrap(),
        );

        // Switch the mic hardware on.
        input_state.set_source_state(
            InputDeviceType::MICROPHONE,
            DEFAULT_MIC_NAME.to_string(),
            DeviceStateSource::HARDWARE,
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
        assert_eq!(
            input_state
                .get_state(InputDeviceType::MICROPHONE, DEFAULT_MIC_NAME.to_string())
                .unwrap(),
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );

        // Switch the camera software on.
        input_state.set_source_state(
            InputDeviceType::CAMERA,
            DEFAULT_CAMERA_NAME.to_string(),
            DeviceStateSource::SOFTWARE,
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
        assert_eq!(
            input_state
                .get_state(InputDeviceType::CAMERA, DEFAULT_CAMERA_NAME.to_string())
                .unwrap(),
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
    }

    #[test]
    fn test_input_configuration_to_input_state() {
        let config = InputConfiguration {
            devices: vec![
                create_device_config(
                    DEFAULT_MIC_NAME,
                    InputDeviceType::MICROPHONE,
                    MUTED_BITS,
                    AVAILABLE_BITS,
                ),
                create_device_config(
                    DEFAULT_CAMERA_NAME,
                    InputDeviceType::CAMERA,
                    AVAILABLE_BITS,
                    AVAILABLE_BITS,
                ),
                create_device_config(
                    "camera2",
                    InputDeviceType::CAMERA,
                    AVAILABLE_BITS,
                    MUTED_DISABLED_BITS,
                ),
            ],
        };
        let result: InputState = config.into();
        assert_eq!(
            result
                .get_source_state(
                    InputDeviceType::MICROPHONE,
                    DEFAULT_MIC_NAME.to_string(),
                    DeviceStateSource::SOFTWARE,
                )
                .unwrap(),
            DeviceState::from_bits(MUTED_BITS).unwrap(),
        );
        assert_eq!(
            result
                .get_source_state(
                    InputDeviceType::MICROPHONE,
                    DEFAULT_MIC_NAME.to_string(),
                    DeviceStateSource::HARDWARE,
                )
                .unwrap(),
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
        assert_eq!(
            result
                .get_source_state(
                    InputDeviceType::CAMERA,
                    DEFAULT_CAMERA_NAME.to_string(),
                    DeviceStateSource::SOFTWARE,
                )
                .unwrap(),
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
        assert_eq!(
            result
                .get_source_state(
                    InputDeviceType::CAMERA,
                    DEFAULT_CAMERA_NAME.to_string(),
                    DeviceStateSource::HARDWARE,
                )
                .unwrap(),
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
        assert_eq!(
            result
                .get_source_state(
                    InputDeviceType::CAMERA,
                    "camera2".to_string(),
                    DeviceStateSource::SOFTWARE,
                )
                .unwrap(),
            DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
        );
        assert_eq!(
            result
                .get_source_state(
                    InputDeviceType::CAMERA,
                    "camera2".to_string(),
                    DeviceStateSource::HARDWARE,
                )
                .unwrap(),
            DeviceState::from_bits(MUTED_DISABLED_BITS).unwrap(),
        );
    }

    #[test]
    /// Test that the combination of the input device's source states results
    /// in the correct overall device state.
    fn test_overall_state() {
        // The last number doesn't matter here, it will be overwritten by the
        // compute_input_state calls.
        let mut mic_available = create_input_device(
            DEFAULT_MIC_NAME,
            InputDeviceType::MICROPHONE,
            AVAILABLE_BITS,
            AVAILABLE_BITS,
            AVAILABLE_BITS,
        );
        let mut mic_disabled = create_input_device(
            DEFAULT_MIC_NAME,
            InputDeviceType::MICROPHONE,
            MUTED_DISABLED_BITS,
            AVAILABLE_BITS,
            MUTED_DISABLED_BITS,
        );
        let mut mic_muted = create_input_device(
            DEFAULT_MIC_NAME,
            InputDeviceType::MICROPHONE,
            AVAILABLE_BITS,
            MUTED_BITS,
            MUTED_BITS,
        );
        let mut mic_active = create_input_device(
            DEFAULT_MIC_NAME,
            InputDeviceType::MICROPHONE,
            3,
            AVAILABLE_BITS,
            3,
        );
        let mut mic_error = create_input_device(
            DEFAULT_MIC_NAME,
            InputDeviceType::MICROPHONE,
            10,
            AVAILABLE_BITS,
            16,
        );

        mic_available.compute_input_state();
        mic_disabled.compute_input_state();
        mic_muted.compute_input_state();
        mic_active.compute_input_state();
        mic_error.compute_input_state();

        assert_eq!(mic_available.state, DeviceState::AVAILABLE);
        assert_eq!(mic_disabled.state, DeviceState::DISABLED | DeviceState::MUTED);
        assert_eq!(mic_muted.state, DeviceState::MUTED);
        assert_eq!(mic_active.state, DeviceState::ACTIVE | DeviceState::AVAILABLE);
        assert_eq!(mic_error.state, DeviceState::ERROR);
    }

    #[test]
    fn test_input_device_to_fidl_input_device() {
        let expected_mic: FidlInputDevice = create_fidl_input_device(
            DEFAULT_MIC_NAME,
            FidlDeviceType::Microphone,
            AVAILABLE_BITS,
            AVAILABLE_BITS,
            AVAILABLE_BITS,
        );
        let expected_cam: FidlInputDevice = create_fidl_input_device(
            DEFAULT_CAMERA_NAME,
            FidlDeviceType::Camera,
            AVAILABLE_BITS,
            MUTED_BITS,
            MUTED_BITS,
        );

        let mut mic = InputDevice::new(DEFAULT_MIC_NAME.to_string(), InputDeviceType::MICROPHONE);
        let _ = mic
            .source_states
            .insert(DeviceStateSource::SOFTWARE, DeviceState::from_bits(AVAILABLE_BITS).unwrap());
        let _ = mic
            .source_states
            .insert(DeviceStateSource::HARDWARE, DeviceState::from_bits(AVAILABLE_BITS).unwrap());
        mic.state = DeviceState::from_bits(AVAILABLE_BITS).unwrap();

        let mut cam = InputDevice::new(DEFAULT_CAMERA_NAME.to_string(), InputDeviceType::CAMERA);
        let _ = cam
            .source_states
            .insert(DeviceStateSource::SOFTWARE, DeviceState::from_bits(AVAILABLE_BITS).unwrap());
        let _ = cam
            .source_states
            .insert(DeviceStateSource::HARDWARE, DeviceState::from_bits(MUTED_BITS).unwrap());
        cam.state = DeviceState::from_bits(MUTED_BITS).unwrap();

        let mic_res: FidlInputDevice = mic.into();
        let cam_res: FidlInputDevice = cam.into();

        verify_fidl_input_device_eq(mic_res, expected_mic);
        verify_fidl_input_device_eq(cam_res, expected_cam);
    }

    #[test]
    fn test_input_device_type_to_string() {
        assert_eq!(InputDeviceType::CAMERA.to_string(), DEFAULT_CAMERA_NAME);
        assert_eq!(InputDeviceType::MICROPHONE.to_string(), DEFAULT_MIC_NAME);
    }

    #[test]
    fn test_fidl_device_type_to_device_type() {
        let cam_res: FidlDeviceType = InputDeviceType::CAMERA.into();
        let mic_res: FidlDeviceType = InputDeviceType::MICROPHONE.into();
        assert_eq!(cam_res, FidlDeviceType::Camera);
        assert_eq!(mic_res, FidlDeviceType::Microphone);
    }

    #[test]
    fn test_device_type_to_fidl_device_type() {
        let cam_res: InputDeviceType = FidlDeviceType::Camera.into();
        let mic_res: InputDeviceType = FidlDeviceType::Microphone.into();
        assert_eq!(cam_res, InputDeviceType::CAMERA);
        assert_eq!(mic_res, InputDeviceType::MICROPHONE);
    }

    #[test]
    fn test_fidl_device_state_source_to_device_state_source() {
        let hw_res: FidlDeviceStateSource = DeviceStateSource::HARDWARE.into();
        let sw_res: FidlDeviceStateSource = DeviceStateSource::SOFTWARE.into();
        assert_eq!(hw_res, FidlDeviceStateSource::Hardware);
        assert_eq!(sw_res, FidlDeviceStateSource::Software);
    }

    #[test]
    fn test_device_state_source_to_fidl_device_state_source() {
        let hw_res: DeviceStateSource = FidlDeviceStateSource::Hardware.into();
        let sw_res: DeviceStateSource = FidlDeviceStateSource::Software.into();
        assert_eq!(hw_res, DeviceStateSource::HARDWARE);
        assert_eq!(sw_res, DeviceStateSource::SOFTWARE);
    }

    #[test]
    fn test_device_state_errors() {
        let available_disabled = DeviceState::from_bits(9).unwrap();
        let available_muted = DeviceState::from_bits(5).unwrap();
        let active_muted = DeviceState::from_bits(6).unwrap();
        let active_disabled = DeviceState::from_bits(10).unwrap();
        assert!(available_disabled.has_error());
        assert!(available_muted.has_error());
        assert!(active_muted.has_error());
        assert!(active_disabled.has_error());
    }

    #[test]
    fn test_fidl_device_state_to_device_state() {
        let device_state: DeviceState = FidlDeviceState {
            toggle_flags: FidlToggleFlags::from_bits(MUTED_BITS),
            ..FidlDeviceState::EMPTY
        }
        .into();
        assert_eq!(device_state, DeviceState::from_bits(MUTED_BITS).unwrap(),);
    }

    #[test]
    fn test_device_state_to_fidl_device_state() {
        let fidl_device_state: FidlDeviceState = DeviceState::from_bits(MUTED_BITS).unwrap().into();
        assert_eq!(
            fidl_device_state,
            FidlDeviceState {
                toggle_flags: FidlToggleFlags::from_bits(MUTED_BITS),
                ..FidlDeviceState::EMPTY
            }
        );
    }
}
