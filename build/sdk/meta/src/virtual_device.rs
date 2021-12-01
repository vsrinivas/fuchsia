// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the virtual_device metadata.

use crate::common::{
    AudioModel, DataUnits, ElementType, Envelope, PointingDevice, ScreenUnits, TargetArchitecture,
};
use crate::json::JsonObject;
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

/// An emulator's execution is defined by Behaviors. These are the source of parameters that get
/// passed to the command line, as well as set up and clean up that happen before and after
/// execution, respectively.
///
/// A virtual device specification will include a collection of Behaviors, all of which will be
/// included unless they implement a filter routine which excludes them based on runtime flags,
/// host capabilities, or other factors. The engine will not generally look for consistency among
/// included Behaviors, but will include everything specified in the manifest that isn't filtered
/// out.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct Behavior {
    /// A human-readable description of the Behaviors attributes, such as what it does, what data
    /// it requires, and any limitations on its use.
    pub description: String,

    /// The data used by this Behavior. Each field is a vector of strings, keyed to another string
    /// to differentiate purposes. For example, a FemuBehavior expects four fields: "args",
    /// "features", kernel_args, and "options". These fields can usually be copied into the
    /// respective place on the generated command line as-is, but the implementing structure is
    /// responsible for knowing which fields go where, and whether they need any additional
    ///  processing.
    pub data: BehaviorData,

    /// The name of the implementing class for this Behavior. A handler is a Rust class which
    /// implements the BehaviorTrait (setup(), cleanup(), and filter()), as well as an
    /// engine-specific trait for retrieving values from the "data" property. If no custom code is
    /// needed for a Behavior, for set up or clean up, etc., there are default handlers available
    /// for each engine. For example: SimpleFemuBehavior copies the appropriate data fields for
    /// Femu - args, features, options, and kernel_args - to the command line; its setup, cleanup,
    /// and filter functions are no-ops.
    ///
    /// When adding a new handler, place the rust code in a new file in
    /// //src/developer/ffx/plugins/emu/engines/src/behaviors/types,
    /// add your handler's name to the BehaviorHandler enumeration in
    /// //src/developer/ffx/plugins/emu/engines/src/behaviors/mod.rs,
    /// and add match branches for deserialization in the engine implementations, such as
    /// //src/developer/ffx/plugins/emu/engines/src/femu/mod.rs.
    pub handler: String,
}

/// Each engine requires a different data set to implement the same behavior. Each engine is
/// optionally supported by each behavior, so these fields are wrapped in Option; a None in any
/// field indicates that engine type is unsupported.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct BehaviorData {
    /// Used by the Fuchsia Emulator (FEMU), and QEMU. See the FemuData definition for specifics.
    pub femu: Option<FemuData>,
}

/// Data required to implement this behavior on a Femu emulator. Note that the Qemu engine uses a
/// subset of the fields for a FemuData, so there is no QemuData type. Femu and Qemu both use this
/// data type.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct FemuData {
    /// Arguments. The set of flags which follow the "-fuchsia" option. These are not processed by
    /// Femu, but are passed through to Qemu.
    pub args: Vec<String>,

    /// Features. A Femu-only field. Features are the first set of command line flags passed to the
    /// Femu binary. These are single words, capitalized, comma-separated, and immediately follow
    /// the flag "-feature".
    pub features: Vec<String>,

    /// Kernel Arguments. The last part of the command line. A set of text values that are passed
    /// through the emulator executable directly to the guest system's kernel.
    pub kernel_args: Vec<String>,

    /// Options. A Femu-only field. Options come immediately after features. Options may be boolean
    /// flags (e.g. -no-hidpi-scaling) or have associated values (e.g. -window-size 1280x800).
    pub options: Vec<String>,
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

    /// List of behaviors to be implemented by the virtual device.
    pub behaviors: HashMap<String, Behavior>,
}

impl JsonObject for Envelope<VirtualDeviceV1> {
    fn get_schema() -> &'static str {
        include_str!("../virtual_device-93A41932.json")
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
                "behaviors": {
                    "test_behavior": {
                        "description": "Text",
                        "handler": "HanderName",
                        "data": {
                            "femu": {
                                "args": [],
                                "features": [],
                                "kernel_args": [],
                                "options": []
                            }
                        }
                    }
                }
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
                },
                "behaviors": {
                    "test_behavior": {
                        "description": "Text",
                        "handler": "HanderName",
                        "data": {
                            "femu": {
                                "args": [],
                                "features": [],
                                "kernel_args": [],
                                "options": []
                            }
                        }
                    }
                }
            }
        }
        "#,
        // Incorrect type
        valid = false,
    }
}
