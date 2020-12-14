// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input::input_device_configuration::InputConfiguration;
use bitflags::bitflags;
use fidl_fuchsia_settings::{
    DeviceState as FidlDeviceState, DeviceStateSource as FidlDeviceStateSource,
    DeviceType as FidlDeviceType, InputDevice as FidlInputDevice, InputState as FidlInputState,
    SourceState as FidlSourceState, ToggleStateFlags as FidlToggleFlags,
};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

// TODO(fxbug.dev/60682): Write tests using input types.
#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
// The top-level struct for the input state. It categorizes the input devices
// by their device type.
pub struct InputState {
    // The input devices categorized by device type.
    pub input_categories: HashMap<InputDeviceType, InputCategory>,
}

impl InputState {
    // Insert a FidlInputState into the internal InputState hierarchy, updating the
    // state if it already exists or adding the state if it does not.
    pub fn insert_state(&mut self, input_state: FidlInputState, source: DeviceStateSource) {
        // Ensure the category has an entry in the categories map.
        let input_device_type = input_state.device_type.unwrap().into();
        self.input_categories.entry(input_device_type).or_insert(InputCategory::new());

        // Ensure the device has an entry in the devices map.
        let category = self.input_categories.get_mut(&input_device_type).unwrap();
        let devices = &mut category.devices;
        let device_name = match input_state.name.clone() {
            Some(name) => name,
            None => "".to_string(),
        };
        devices
            .entry(device_name.clone())
            .or_insert(InputDevice::new(device_name.clone(), input_device_type));

        // Ensure the source state has an entry in the source_states map.
        let device = devices.get_mut(&device_name).unwrap();
        let source_states = &mut device.source_states;
        if let Some(fidl_state) = input_state.state.clone() {
            source_states.insert(source, fidl_state.into());
            device.compute_input_state();
        };
    }
}

impl From<InputConfiguration> for InputState {
    fn from(config: InputConfiguration) -> Self {
        let mut categories = HashMap::<InputDeviceType, InputCategory>::new();
        let devices = config.devices;

        devices.iter().for_each(|device_config| {
            // Ensure the category has an entry in the categories map.
            let input_device_type = device_config.device_type;
            categories.entry(input_device_type).or_insert(InputCategory::new());

            // Ensure the device has an entry in the devices map.
            let category = categories.get_mut(&input_device_type).unwrap();
            let devices = &mut category.devices;
            let device_name = device_config.device_name.clone();
            devices
                .entry(device_name.clone())
                .or_insert(InputDevice::new(device_name.clone(), input_device_type));

            // Set the entry on the source states map.
            let device = devices.get_mut(&device_name).unwrap();
            device_config.source_states.iter().for_each(|source_state| {
                let value =
                    DeviceState::from_bits(source_state.state).unwrap_or(DeviceState::new());
                device.source_states.insert(source_state.source, value);
            });

            // Recompute the overall state.
            device.compute_input_state();
        });
        InputState { input_categories: categories }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct InputCategory {
    // Map of input devices in this category, identified by names.
    // It is recommended that the name be the lower-case string
    // representation of the device type if there is only one input
    // device in this category.
    pub devices: HashMap<String, InputDevice>,
}

impl InputCategory {
    pub fn new() -> Self {
        Self { devices: HashMap::<String, InputDevice>::new() }
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct InputDevice {
    // The unique name within the device type.
    pub name: String,

    // The type of input device.
    pub device_type: InputDeviceType,

    // The states for each source.
    pub source_states: HashMap<DeviceStateSource, DeviceState>,

    // The overall state of the device considering the |source_state|s.
    pub state: DeviceState,
}

impl InputDevice {
    pub fn new(name: String, device_type: InputDeviceType) -> Self {
        Self {
            name,
            device_type,
            source_states: HashMap::<DeviceStateSource, DeviceState>::new(),
            state: DeviceState::new(),
        }
    }

    pub fn compute_input_state(&mut self) {
        // TODO(fxbug.dev/60682): compute input state based on combined source states.
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
                    source_state.source = Some(source.clone().into());
                    source_state.state = Some(
                        source_state_map
                            .get(&source)
                            .expect("Source state map key missing")
                            .clone()
                            .into(),
                    );
                    return source_state;
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

impl DeviceState {
    pub fn new() -> Self {
        // Represents AVAILABLE as the default.
        Self { bits: 1 }
    }

    // The flags that clients can manipulate by default.
    pub fn default_mutable_toggle_state() -> Self {
        DeviceState::MUTED | DeviceState::DISABLED
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
