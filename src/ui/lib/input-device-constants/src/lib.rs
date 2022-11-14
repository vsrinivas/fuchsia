// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Generic types of supported input devices.
#[derive(Debug, Clone, Copy, Eq, Hash, PartialEq)]
// LINT.IfChange
pub enum InputDeviceType {
    Keyboard,
    LightSensor,
    ConsumerControls,
    Mouse,
    Touch,
}
// LINT.ThenChange(/src/lib/assembly/config_schema/src/product_config.rs)

impl InputDeviceType {
    /// Parses an `InputDeviceType` string that has been serialized from
    /// `src/lib/assembly/config_schema/src/product_config.rs`: `InputDeviceType`, which has
    /// slightly different names.
    ///
    /// If a device string isn't recognized, returns `None`.
    pub fn try_from_assembly_config_entry(device: impl AsRef<str>) -> Option<Self> {
        match device.as_ref() {
            "button" => Some(Self::ConsumerControls),
            "keyboard" => Some(Self::Keyboard),
            "lightsensor" => Some(Self::LightSensor),
            "mouse" => Some(Self::Mouse),
            "touchscreen" => Some(Self::Touch),
            _ => None,
        }
    }

    /// Parses a list of supported `InputDeviceType`s from a structured configuration
    /// `supported_input_devices` list. Unknown device types are logged and skipped.
    pub fn list_from_structured_config_list<'a, V, T>(list: V) -> Vec<Self>
    where
        V: IntoIterator<Item = &'a T>,
        T: AsRef<str> + 'a,
    {
        list.into_iter()
            .filter_map(|device| match Self::try_from_assembly_config_entry(device) {
                Some(d) => Some(d),
                None => {
                    tracing::warn!(
                        "Ignoring unsupported device configuration: {}",
                        device.as_ref()
                    );
                    None
                }
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn input_device_list_from_structured_config_list() {
        let config = vec![
            "touchscreen".to_string(),
            "button".to_string(),
            "keyboard".to_string(),
            "mouse".to_string(),
            "hamster".to_string(),
            "lightsensor".to_string(),
        ];
        let expected = vec![
            InputDeviceType::Touch,
            InputDeviceType::ConsumerControls,
            InputDeviceType::Keyboard,
            InputDeviceType::Mouse,
            InputDeviceType::LightSensor,
        ];
        let actual = InputDeviceType::list_from_structured_config_list(&config);
        assert_eq!(actual, expected);
    }

    #[test]
    fn input_device_list_from_structured_config_list_strs() {
        let config = ["hamster", "button", "keyboard", "mouse"];
        let expected = vec![
            InputDeviceType::ConsumerControls,
            InputDeviceType::Keyboard,
            InputDeviceType::Mouse,
        ];
        let actual = InputDeviceType::list_from_structured_config_list(&config);
        assert_eq!(actual, expected);
    }
}
