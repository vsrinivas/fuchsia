// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the virtual_device metadata.

use crate::common::{
    AudioModel, DataUnits, ElementType, Envelope, PointingDevice, ScreenUnits, TargetArchitecture,
};
use crate::json::{schema, JsonObject};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Specifics for a CPU.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Cpu {
    /// Target CPU architecture.
    pub arch: TargetArchitecture,
}

/// Details of virtual input devices, such as mice.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct InputDevice {
    /// Pointing device for interacting with the target.
    pub pointing_device: PointingDevice,
}

/// Details of the virtual device's audio interface, if any.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct AudioDevice {
    /// The model of the emulated audio device, or None.
    pub model: AudioModel,
}

/// Screen dimensions for the virtual device, if any.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Screen {
    pub height: usize,
    pub width: usize,
    pub units: ScreenUnits,
}

/// A generic data structure for indicating quantities of data.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct DataAmount {
    pub quantity: usize,
    pub units: DataUnits,
}

/// Specifics for a given platform.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Hardware {
    /// Details of the Central Processing Unit (CPU).
    pub cpu: Cpu,

    /// Details about any audio devices included in the virtual device.
    pub audio: AudioDevice,

    /// The size of the disk image for the virtual device, equivalent to virtual storage capacity.
    pub storage: DataAmount,

    /// Details about any input devices, such as a mouse or touchscreen.
    pub inputs: InputDevice,

    /// Amount of memory in the virtual device.
    pub memory: DataAmount,

    /// The size of the virtual device's screen, measured in pixels.
    pub window_size: Screen,
}

/// Description of a virtual (rather than physical) hardware device.
///
/// This does not include the data "envelope", i.e. it begins within /data in
/// the source json file.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct VirtualDeviceV1 {
    /// A unique name identifying the virtual device specification.
    pub name: String,

    /// An optional human readable description.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,

    /// Always "virtual_device" for a VirtualDeviceV1. This is valuable for
    /// debugging or when writing this record to a json string.
    #[serde(rename = "type")]
    pub kind: ElementType,

    /// Details about the properties of the device.
    pub hardware: Hardware,

    /// An optional path to the file containing the start-up arguments Handlebars template.
    /// TODO(fxbug.dev/90948): Make this non-optional, as soon as the template file is included
    /// with the SDK.
    pub start_up_args_template: Option<String>,

    /// A map of names to port numbers. These are the ports that need to be available to the
    /// virtual device, though a given use case may not require all of them. When emulating with
    /// user-mode networking, these must be mapped to host-side ports to allow communication into
    /// the emulator from external tools (such as ssh and mDNS). When emulating with Tun/Tap mode
    /// networking port mapping is superfluous, so we expect this field to be ignored.
    pub ports: Option<HashMap<String, u16>>,
}

impl JsonObject for Envelope<VirtualDeviceV1> {
    fn get_schema() -> &'static str {
        include_str!("../virtual_device-93A41932.json")
    }

    fn get_referenced_schemata() -> &'static [&'static str] {
        &[schema::COMMON, schema::HARDWARE_V1]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    test_validation! {
        name = test_validation,
        kind = Envelope::<VirtualDeviceV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/virtual_device-93A41932.json",
            "data": {
                "name": "generic-x64",
                "type": "virtual_device",
                "hardware": {
                    "audio": {
                        "model": "hda"
                    },
                    "cpu": {
                        "arch": "x64"
                    },
                    "inputs": {
                        "pointing_device": "touch"
                    },
                    "window_size": {
                        "width": 640,
                        "height": 480,
                        "units": "pixels"
                    },
                    "memory": {
                        "quantity": 1,
                        "units": "gigabytes"
                    },
                    "storage": {
                        "quantity": 1,
                        "units": "gigabytes"
                    }
                },
                "start_up_args_template": "/path/to/args"
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = Envelope::<VirtualDeviceV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/virtual_device-93A41932.json",
            "data": {
                "name": "generic-x64",
                "type": "cc_prebuilt_library",
                "hardware": {
                    "audio": {
                        "model": "hda"
                    },
                    "cpu": {
                        "arch": "x64"
                    },
                    "inputs": {
                        "pointing_device": "touch"
                    },
                    "window_size": {
                        "width": 640,
                        "height": 480,
                        "units": "pixels"
                    },
                    "memory": {
                        "quantity": 1,
                        "units": "gigabytes"
                    },
                    "storage": {
                        "quantity": 1,
                        "units": "gigabytes"
                    }
                }
            }
        }
        "#,
        // Incorrect type
        valid = false,
    }
}
