// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::common::ElementType;
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DeviceProfile {
    pub name: String,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub description: String,
    pub images_url: String,
    pub packages_url: String,
}

impl JsonObject for DeviceProfile {
    fn get_schema() -> &'static str {
        include_str!("../device_profile.json")
    }
}

#[cfg(test)]
mod tests {
    use super::DeviceProfile;

    test_validation! {
        name = test_validation,
        kind = DeviceProfile,
        data = r#"
        {
            "name": "foobar",
            "type": "device_profile",
            "description": "This is Foobar",
            "images_url": "gs://images/foobar",
            "packages_url": "gs://packages/foobar"
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = DeviceProfile,
        data = r#"
        {
            "name": "foobar",
            "type": "fidl_library",
            "description": "This is Foobar",
            "images_url": "gs://images/foobar",
            "packages_url": "gs://packages/foobar"
        }
        "#,
        // Type is invalid.
        valid = false,
    }
}
