// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::normalize_field_key;
use cm_rust::{ConfigDecl, ConfigField, ConfigNestedValueType, ConfigValueType};
use handlebars::Handlebars;
use serde::Serialize;

/// Create a custom FIDL library source file containing all the fields of a config declaration
pub fn create_fidl_source(config_decl: &ConfigDecl, library_name: String) -> String {
    let fidl_library = FidlLibrary::new(config_decl, library_name);
    let mut hbars = Handlebars::new();
    hbars.set_strict_mode(true);
    hbars.register_escape_fn(handlebars::no_escape);
    hbars
        .register_template_string("fidl_library", FIDL_CLIENT_TEMPLATE)
        .expect("known good template should parse");
    hbars.render("fidl_library", &fidl_library).unwrap()
}

// TODO(http://fxbug.dev/90690): The type identifier for the configuration fields struct
// should be user-definable
static FIDL_CLIENT_TEMPLATE: &str = "
library {{library_name}};

type Config = struct {
{{#each struct_fields}}
  {{key}} {{type_}};
{{/each}}
};
";

#[derive(Debug, Serialize)]
struct FidlLibrary {
    library_name: String,
    struct_fields: Vec<FidlStructField>,
}

impl FidlLibrary {
    fn new(config_decl: &ConfigDecl, library_name: String) -> Self {
        FidlLibrary {
            library_name,
            struct_fields: config_decl.fields.iter().map(FidlStructField::from_decl).collect(),
        }
    }
}

#[derive(Debug, Serialize)]
struct FidlStructField {
    key: String,
    type_: String,
}

impl FidlStructField {
    fn from_decl(field: &ConfigField) -> Self {
        FidlStructField {
            key: normalize_field_key(&field.key),
            type_: config_value_type_to_fidl_type(&field.type_),
        }
    }
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
