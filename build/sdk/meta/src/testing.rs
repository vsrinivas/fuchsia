// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::json::JsonObject;

/// A metadata object for testing purposes.
#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct TestObject {
    pub name: String,
    pub files: Vec<String>,
}

impl JsonObject for TestObject {
    fn get_schema() -> &'static str {
        r#"
        {
            "$schema": "http://json-schema.org/draft-04/schema#",
            "description": "Some test object",
            "type": "object",
            "properties": {
                "name": {
                    "type": "string"
                },
                "files": {
                    "type": "array",
                    "items": {
                        "type": "string"
                    }
                }
            }
        }
        "#
    }
}

/// Generates a test verifying that a metadata objects validates properly.
#[cfg(test)]
macro_rules! test_validation {
    (
        name = $name:ident,
        kind = $kind:ident,
        data = $data:expr,
        valid = $valid:expr,
    ) => {
        #[test]
        fn $name() {
            use crate::json::JsonObject;

            let object =
                $kind::new($data.as_bytes()).expect("Metadata object could not be deserialized");
            if $valid {
                object.validate().expect("Validation should have succeeded");
            } else {
                assert!(object.validate().is_err(), "Validation should have failed");
            }
        }
    };
}
