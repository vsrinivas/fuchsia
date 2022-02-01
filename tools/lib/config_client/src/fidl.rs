// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::normalize_field_key;
use cm_rust::{ConfigDecl, ConfigField, ConfigNestedValueType, ConfigValueType};

/// Create a custom FIDL library source file containing all the fields of a config declaration
pub fn create_fidl_source(config_decl: &ConfigDecl, library_name: String) -> String {
    let mut fidl_struct_fields = vec![];
    for ConfigField { key, type_ } in &config_decl.fields {
        let fidl_type = config_value_type_to_fidl_type(&type_);
        let key = normalize_field_key(key);
        let fidl_struct_field = format!("{} {};", key, fidl_type);
        fidl_struct_fields.push(fidl_struct_field);
    }

    // TODO(http://fxbug.dev/90690): The type identifier for the configuration fields struct
    // should be user-definable
    let output = format!(
        "library {};
type Config = struct {{
{}
}};",
        library_name,
        fidl_struct_fields.join("\n")
    );
    output
}

fn config_value_type_to_fidl_type(value_type: &ConfigValueType) -> String {
    match value_type {
        ConfigValueType::Bool => "bool".to_string(),
        ConfigValueType::Uint8 => "uint8".to_string(),
        ConfigValueType::Uint16 => "uint16".to_string(),
        ConfigValueType::Uint32 => "uint32".to_string(),
        ConfigValueType::Uint64 => "uint64".to_string(),
        ConfigValueType::Int8 => "int8".to_string(),
        ConfigValueType::Int16 => "int16".to_string(),
        ConfigValueType::Int32 => "int32".to_string(),
        ConfigValueType::Int64 => "int64".to_string(),
        ConfigValueType::String { max_size } => format!("string:{}", max_size),
        ConfigValueType::Vector { max_count, nested_type } => {
            format!("vector<{}>:{}", config_nested_value_type_to_fidl_type(nested_type), max_count)
        }
    }
}

fn config_nested_value_type_to_fidl_type(nested_type: &ConfigNestedValueType) -> String {
    match nested_type {
        ConfigNestedValueType::Bool => "bool".to_string(),
        ConfigNestedValueType::Uint8 => "uint8".to_string(),
        ConfigNestedValueType::Uint16 => "uint16".to_string(),
        ConfigNestedValueType::Uint32 => "uint32".to_string(),
        ConfigNestedValueType::Uint64 => "uint64".to_string(),
        ConfigNestedValueType::Int8 => "int8".to_string(),
        ConfigNestedValueType::Int16 => "int16".to_string(),
        ConfigNestedValueType::Int32 => "int32".to_string(),
        ConfigNestedValueType::Int64 => "int64".to_string(),
        ConfigNestedValueType::String { max_size } => {
            format!("string:{}", max_size)
        }
    }
}
