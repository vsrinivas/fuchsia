// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{normalize_field_key, SourceGenError};
use cm_rust::{ConfigChecksum, ConfigDecl, ConfigField, ConfigNestedValueType, ConfigValueType};
use handlebars::{handlebars_helper, Handlebars};
use serde::Serialize;

static CC_SOURCE_TEMPLATE: &str = include_str!("../templates/cpp_elf.cc.hbs");
static H_SOURCE_TEMPLATE: &str = include_str!("../templates/cpp_elf.h.hbs");

pub struct CppSource {
    pub cc_source: String,
    pub h_source: String,
}

pub fn create_cpp_elf_wrapper(
    config_decl: &ConfigDecl,
    cpp_namespace: String,
    fidl_library_name: String,
) -> Result<CppSource, SourceGenError> {
    let vars = TemplateVars::from_decl(config_decl, cpp_namespace, fidl_library_name);

    let mut hbars = Handlebars::new();
    hbars.set_strict_mode(true);
    hbars.register_escape_fn(handlebars::no_escape);
    hbars.register_helper("hex_byte", Box::new(hex_byte));
    hbars.register_helper("is_string_vector", Box::new(is_string_vector));
    hbars.register_helper("is_vector", Box::new(is_vector));
    hbars.register_helper("is_string", Box::new(is_string));
    hbars.register_helper("cpp_type", Box::new(cpp_type));
    hbars.register_helper("inspect_type", Box::new(inspect_type));
    hbars.register_template_string("cc_source", CC_SOURCE_TEMPLATE).pretty_unwrap();
    hbars.register_template_string("h_source", H_SOURCE_TEMPLATE).pretty_unwrap();
    let cc_source = hbars.render("cc_source", &vars).pretty_unwrap();
    let h_source = hbars.render("h_source", &vars).pretty_unwrap();
    Ok(CppSource { cc_source, h_source })
}

#[derive(Debug, Serialize)]
struct TemplateVars {
    header_guard: String,
    cpp_namespace: String,
    fidl_library_name: String,
    fidl_cpp_namespace: String,
    expected_checksum: Vec<u8>,
    fields: Vec<Field>,
}

impl TemplateVars {
    fn from_decl(
        config_decl: &ConfigDecl,
        cpp_namespace: String,
        fidl_library_name: String,
    ) -> Self {
        let cpp_namespace = cpp_namespace.replace('.', "_").replace('-', "_").to_ascii_lowercase();
        let header_guard = fidl_library_name.replace('.', "_").to_ascii_uppercase();
        let fidl_cpp_namespace = fidl_library_name.replace('.', "_").to_ascii_lowercase();
        let ConfigChecksum::Sha256(expected_checksum) = &config_decl.checksum;
        let expected_checksum = expected_checksum.to_vec();

        Self {
            fields: config_decl.fields.iter().map(Field::from_decl).collect(),
            header_guard,
            fidl_library_name,
            fidl_cpp_namespace,
            cpp_namespace,
            expected_checksum,
        }
    }
}

#[derive(Debug, Serialize)]
struct Field {
    ident: String,
    type_: ConfigValueType,
}

impl Field {
    fn from_decl(field: &ConfigField) -> Self {
        Field { ident: normalize_field_key(&field.key), type_: field.type_.clone() }
    }
}

handlebars_helper!(hex_byte: |b: u8| format!("{:#04x}", b));
handlebars_helper!(is_string_vector: |t: ConfigValueType| matches!(t, ConfigValueType::Vector {
    nested_type: ConfigNestedValueType::String { .. }, ..
}));
handlebars_helper!(is_vector: |t: ConfigValueType| matches!(t, ConfigValueType::Vector { .. }));
handlebars_helper!(is_string: |t: ConfigValueType| matches!(t, ConfigValueType::String { .. }));
handlebars_helper!(cpp_type: |t: ConfigValueType| match t {
    ConfigValueType::Bool => "bool",
    ConfigValueType::Uint8 => "uint8_t",
    ConfigValueType::Uint16 => "uint16_t",
    ConfigValueType::Uint32 => "uint32_t",
    ConfigValueType::Uint64 => "uint64_t",
    ConfigValueType::Int8 => "int8_t",
    ConfigValueType::Int16 => "int16_t",
    ConfigValueType::Int32 => "int32_t",
    ConfigValueType::Int64 => "int64_t",
    ConfigValueType::String { .. } => "std::string",
    ConfigValueType::Vector { nested_type, .. } => match nested_type {
        ConfigNestedValueType::Bool => "std::vector<bool>",
        ConfigNestedValueType::Uint8 => "std::vector<uint8_t>",
        ConfigNestedValueType::Uint16 => "std::vector<uint16_t>",
        ConfigNestedValueType::Uint32 => "std::vector<uint32_t>",
        ConfigNestedValueType::Uint64 => "std::vector<uint64_t>",
        ConfigNestedValueType::Int8 => "std::vector<int8_t>",
        ConfigNestedValueType::Int16 => "std::vector<int16_t>",
        ConfigNestedValueType::Int32 => "std::vector<int32_t>",
        ConfigNestedValueType::Int64 => "std::vector<int64_t>",
        ConfigNestedValueType::String { .. } => "std::vector<std::string>",
    },
});
handlebars_helper!(inspect_type: |t: ConfigValueType| match t {
    ConfigValueType::Bool => "Bool",
    ConfigValueType::Uint8
    | ConfigValueType::Uint16
    | ConfigValueType::Uint32
    | ConfigValueType::Uint64 => "Uint",
    ConfigValueType::Int8
    | ConfigValueType::Int16
    | ConfigValueType::Int32
    | ConfigValueType::Int64 => "Int",
    ConfigValueType::String { .. } => "String",
    ConfigValueType::Vector { nested_type, .. } => match nested_type {
        // inspect doesn't provide a CreateBoolArray so cast bools to uints
        ConfigNestedValueType::Bool => "Uint",
        ConfigNestedValueType::Uint8
        | ConfigNestedValueType::Uint16
        | ConfigNestedValueType::Uint32
        | ConfigNestedValueType::Uint64 => "Uint",
        ConfigNestedValueType::Int8
        | ConfigNestedValueType::Int16
        | ConfigNestedValueType::Int32
        | ConfigNestedValueType::Int64 => "Int",
        ConfigNestedValueType::String { .. } => "String",
    },
});

/// Trait for unwrapping results with Display errors so we can see template syntax errors correctly.
trait ResultExt<T> {
    fn pretty_unwrap(self) -> T;
}

impl<T, E> ResultExt<T> for Result<T, E>
where
    E: std::fmt::Display,
{
    fn pretty_unwrap(self) -> T {
        match self {
            Ok(t) => t,
            Err(e) => panic!("unwrap failed: {}", e),
        }
    }
}
