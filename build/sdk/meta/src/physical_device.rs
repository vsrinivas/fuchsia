// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the physical_device metadata.

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

/// Specifics for a given hardware platform.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Hardware {
    /// Details of the Central Processing Unit (CPU).
    cpu: Cpu,
}

/// Description of a physical (rather than virtual) hardware device.
///
/// This does not include the data "envelope", i.e. it begins within /data in
/// the source json file.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct PhysicalDeviceV1 {
    /// A unique name identifying the physical device specification.
    pub name: String,

    /// An optional human readable description.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,

    /// Always "physical_device" for a PhysicalDeviceSpec. This is valuable for
    /// debugging or when writing this record to a json string.
    #[serde(rename = "type")]
    pub kind: ElementType,

    /// Details about the properties of the device.
    pub hardware: Hardware,
}

impl JsonObject for Envelope<PhysicalDeviceV1> {
    fn get_schema() -> &'static str {
        include_str!("../physical_device-0bd5d21f.json")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    test_validation! {
        name = test_validation,
        kind = Envelope::<PhysicalDeviceV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/physical_device-0bd5d21f.json",
            "data": {
                "name": "generic-x64",
                "type": "physical_device" ,
                "hardware": {
                   "cpu": {
                       "arch": "x64"
                   }
                }
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = Envelope::<PhysicalDeviceV1>,
        data = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/physical_device-0bd5d21f.json",
            "data": {
                "name": "generic-x64",
                "type": "cc_prebuilt_library",
                "hardware": {
                   "cpu": {
                       "arch": "x64"
                   }
                }
            }
        }
        "#,
        // Incorrect type
        valid = false,
    }
}
