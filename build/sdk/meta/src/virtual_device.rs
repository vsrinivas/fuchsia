// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the virtual_device metadata.

use crate::common::{ElementType, Envelope, TargetArchitecture};
use crate::json::JsonObject;
use serde::{Deserialize, Serialize};

/// Specifics for a CPU.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Cpu {
    /// Target CPU architecture.
    arch: TargetArchitecture,
}

/// Specifics for a given platform.
/// Work in progress properties, unchecked by json-schema.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct Wip {
    cpu: Cpu,
    audio: bool,
    pointing_device: String,
    window_width: u32,
    window_height: u32,
    ram_mb: u64,
    image_size: String,
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
    /// Work in progress properties, unchecked by json-schema. May be renamed
    /// to virtual_hardware or similar in the future.
    pub wip: Wip,
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
                "type": "virtual_device" ,
                "wip": {
                    "cpu": {
                        "arch": "x64"
                    },
                    "audio": true,
                    "pointing_device": "touch",
                    "window_width": 1280,
                    "window_height": 800,
                    "ram_mb": 8192,
                    "image_size": "2G"
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
                "wip": {
                    "cpu": {
                        "arch": "x64"
                    },
                    "audio": true,
                    "pointing_device": "touch",
                    "window_width": 1280,
                    "window_height": 800,
                    "ram_mb": 8192,
                    "image_size": "2G"
                }
            }
        }
        "#,
        // Incorrect type
        valid = false,
    }
}
