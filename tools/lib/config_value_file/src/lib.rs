// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! A library for creating configuration value files as described in [Fuchsia RFC-0127].
//!
//! [Fuchsia RFC-0127]: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0127_structured_configuration

mod field;

use crate::field::{config_value_from_json_value, FieldError};
use cm_rust::{ConfigDecl, ValueSpec, ValuesData};
use serde_json::Value as JsonValue;
use std::collections::BTreeMap;

/// Create a configuration value file from the compiled manifest's config declaration and a map of
/// configuration keys to JSON values.
// TODO(https://fxbug.dev/86797) decide on a better interface than json values?
pub fn populate_value_file(
    config_decl: &ConfigDecl,
    mut json_values: BTreeMap<String, JsonValue>,
) -> Result<ValuesData, FileError> {
    let values = config_decl
        .fields
        .iter()
        .map(|field| {
            let json_value = json_values
                .remove(&field.key)
                .ok_or_else(|| FileError::MissingValue { key: field.key.clone() })?;
            let value = config_value_from_json_value(&json_value, &field.type_)
                .map_err(|reason| FileError::InvalidField { key: field.key.clone(), reason })?;
            Ok(ValueSpec { value })
        })
        .collect::<Result<Vec<ValueSpec>, _>>()?;

    // we remove the definitions from the values map above, so any remaining keys are undefined
    // in the manifest
    if !json_values.is_empty() {
        return Err(FileError::ExtraValues { keys: json_values.into_keys().collect() });
    }

    Ok(ValuesData { values, checksum: config_decl.checksum.clone() })
}

/// Error from working with a configuration value file.
#[derive(Debug, thiserror::Error, PartialEq)]
#[allow(missing_docs)]
pub enum FileError {
    #[error("Invalid config field `{key}`")]
    InvalidField {
        key: String,
        #[source]
        reason: FieldError,
    },

    #[error("`{key}` field in manifest does not have a value defined.")]
    MissingValue { key: String },

    #[error("Fields `{keys:?}` in value definition do not exist in manifest.")]
    ExtraValues { keys: Vec<String> },
}

#[cfg(test)]
mod tests {
    use super::{field::JsonTy, *};
    use cm_rust::{ConfigChecksum, ListValue::*, SingleValue::*, Value::*};
    use fidl_fuchsia_component_config_ext::{config_decl, values_data};
    use serde_json::json;

    fn test_checksum() -> ConfigChecksum {
        // sha256("Back to the Fuchsia")
        ConfigChecksum::Sha256([
            0xb5, 0xf9, 0x33, 0xe8, 0x94, 0x56, 0x3a, 0xf9, 0x61, 0x39, 0xe5, 0x05, 0x79, 0x4b,
            0x88, 0xa5, 0x3e, 0xd4, 0xd1, 0x5c, 0x32, 0xe2, 0xb4, 0x49, 0x9e, 0x42, 0xeb, 0xa3,
            0x32, 0xb1, 0xf5, 0xbb,
        ])
    }

    #[test]
    fn basic_success() {
        let decl = config_decl! {
            ck@ test_checksum(),
            my_flag: { bool },
            my_uint8: { uint8 },
            my_uint16: { uint16 },
            my_uint32: { uint32 },
            my_uint64: { uint64 },
            my_int8: { int8 },
            my_int16: { int16 },
            my_int32: { int32 },
            my_int64: { int64 },
            my_string: { string, max_size: 100 },
            my_vector_of_flag: { vector, element: bool, max_count: 100 },
            my_vector_of_uint8: { vector, element: uint8, max_count: 100 },
            my_vector_of_uint16: { vector, element: uint16, max_count: 100 },
            my_vector_of_uint32: { vector, element: uint32, max_count: 100 },
            my_vector_of_uint64: { vector, element: uint64, max_count: 100 },
            my_vector_of_int8: { vector, element: int8, max_count: 100 },
            my_vector_of_int16: { vector, element: int16, max_count: 100 },
            my_vector_of_int32: { vector, element: int32, max_count: 100 },
            my_vector_of_int64: { vector, element: int64, max_count: 100 },
            my_vector_of_string: {
                vector,
                element: { string, max_size: 100 },
                max_count: 100
            },
        };

        let values: BTreeMap<String, serde_json::Value> = serde_json::from_value(json!({
            "my_flag": false,
            "my_uint8": 255u8,
            "my_uint16": 65535u16,
            "my_uint32": 4000000000u32,
            "my_uint64": 8000000000u64,
            "my_int8": -127i8,
            "my_int16": -32766i16,
            "my_int32": -2000000000i32,
            "my_int64": -4000000000i64,
            "my_string": "hello, world!",
            "my_vector_of_flag": [ true, false ],
            "my_vector_of_uint8": [ 1, 2, 3 ],
            "my_vector_of_uint16": [ 2, 3, 4 ],
            "my_vector_of_uint32": [ 3, 4, 5 ],
            "my_vector_of_uint64": [ 4, 5, 6 ],
            "my_vector_of_int8": [ -1, -2, 3 ],
            "my_vector_of_int16": [ -2, -3, 4 ],
            "my_vector_of_int32": [ -3, -4, 5 ],
            "my_vector_of_int64": [ -4, -5, 6 ],
            "my_vector_of_string": [ "hello, world!", "hello, again!" ],
        }))
        .unwrap();

        let expected = values_data![
            ck@ test_checksum(),
            Single(Flag(false)),
            Single(Unsigned8(255u8)),
            Single(Unsigned16(65535u16)),
            Single(Unsigned32(4000000000u32)),
            Single(Unsigned64(8000000000u64)),
            Single(Signed8(-127i8)),
            Single(Signed16(-32766i16)),
            Single(Signed32(-2000000000i32)),
            Single(Signed64(-4000000000i64)),
            Single(Text("hello, world!".into())),
            List(FlagList(vec![true, false])),
            List(Unsigned8List(vec![1, 2, 3])),
            List(Unsigned16List(vec![2, 3, 4])),
            List(Unsigned32List(vec![3, 4, 5])),
            List(Unsigned64List(vec![4, 5, 6])),
            List(Signed8List(vec![-1, -2, 3])),
            List(Signed16List(vec![-2, -3, 4])),
            List(Signed32List(vec![-3, -4, 5])),
            List(Signed64List(vec![-4, -5, 6])),
            List(TextList(vec!["hello, world!".into(), "hello, again!".into()])),
        ];

        let observed = populate_value_file(&decl, values).unwrap();
        assert_eq!(observed, expected);
    }

    #[test]
    fn invalid_field_is_correctly_identified() {
        let decl = config_decl! {
            ck@ test_checksum(),
            my_flag: { bool },
            my_uint8: { uint8 },
        };

        let values: BTreeMap<String, serde_json::Value> = serde_json::from_value(json!({
            "my_flag": false,
            "my_uint8": true,
        }))
        .unwrap();

        assert_eq!(
            populate_value_file(&decl, values).unwrap_err(),
            FileError::InvalidField {
                key: "my_uint8".to_string(),
                reason: FieldError::JsonTypeMismatch {
                    expected: JsonTy::Number,
                    received: JsonTy::Bool
                }
            }
        );
    }

    #[test]
    fn all_keys_must_be_defined() {
        let decl = config_decl! {
            ck@ test_checksum(),
            my_flag: { bool },
            my_uint8: { uint8 },
        };

        let values: BTreeMap<String, serde_json::Value> = serde_json::from_value(json!({
            "my_flag": false,
        }))
        .unwrap();

        assert_eq!(
            populate_value_file(&decl, values).unwrap_err(),
            FileError::MissingValue { key: "my_uint8".to_string() }
        );
    }

    #[test]
    fn no_extra_keys_can_be_defined() {
        let decl = config_decl! {
            ck@ test_checksum(),
            my_flag: { bool },
        };

        let values: BTreeMap<String, serde_json::Value> = serde_json::from_value(json!({
            "my_flag": false,
            "my_uint8": 1,
            "my_uint16": 2,
        }))
        .unwrap();

        assert_eq!(
            populate_value_file(&decl, values).unwrap_err(),
            FileError::ExtraValues { keys: vec!["my_uint16".into(), "my_uint8".into()] }
        );
    }
}
