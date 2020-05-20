// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::common::ElementType;
use crate::json::JsonObject;

/// SDK data object, containing either config or license files.
/// See //build/sdk/sdk_data.gni for details.
#[derive(Serialize, Deserialize, Debug, Clone, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Data {
    pub name: String,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub data: Vec<String>,
}

impl JsonObject for Data {
    fn get_schema() -> &'static str {
        // Relative path to the sdk_data metadata schema.
        include_str!("../data.json")
    }
}

#[cfg(test)]
mod tests {
    use super::Data;

    test_validation! {
        name = test_validation_config,
        kind = Data,
        data = r#"
        {
            "name": "foobar",
            "type": "config",
            "data": [
                "data/config/foobar/config.json"
            ]
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_config_invalid,
        kind = Data,
        data = r#"
        {
            "name": "foobar",
            "type": "config",
            "data": []
        }
        "#,
        // Data is empty.
        valid = false,
    }

    test_validation! {
        name = test_validation_license,
        kind = Data,
        data = r#"
        {
            "name": "foobar",
            "type": "license",
            "data": [
                "data/license/foobar/LICENSE"
            ]
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_license_invalid,
        kind = Data,
        data = r#"
        {
            "name": "foobar",
            "type": "license",
            "data": []
        }
        "#,
        // Data is empty.
        valid = false,
    }
}
