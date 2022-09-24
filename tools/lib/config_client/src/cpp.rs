// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{normalize_field_key, SourceGenError};
use cm_rust::{ConfigChecksum, ConfigDecl, ConfigField, ConfigNestedValueType, ConfigValueType};
use handlebars::{handlebars_helper, Handlebars};
use serde::Serialize;
use std::str::FromStr;

static CC_ELF_SOURCE_TEMPLATE: &str = include_str!("../templates/cpp_elf.cc.hbs");
static H_ELF_SOURCE_TEMPLATE: &str = include_str!("../templates/cpp_elf.h.hbs");

static CC_ELF_HLCPP_SOURCE_TEMPLATE: &str = include_str!("../templates/cpp_elf_hlcpp.cc.hbs");

static CC_DRIVER_SOURCE_TEMPLATE: &str = include_str!("../templates/cpp_driver.cc.hbs");
static H_DRIVER_SOURCE_TEMPLATE: &str = include_str!("../templates/cpp_driver.h.hbs");

static HELPERS_SOURCE_TEMPLATE: &str = include_str!("../templates/helpers.cc.hbs");
static TYPEDEF_SOURCE_TEMPLATE: &str = include_str!("../templates/typedef.h.hbs");
static VMO_PARSE_SOURCE_TEMPLATE: &str = include_str!("../templates/vmo_parse.cc.hbs");
static VMO_PARSE_HELPERS_SOURCE_TEMPLATE: &str =
    include_str!("../templates/vmo_parse_helpers.cc.hbs");

pub struct CppSource {
    pub cc_source: String,
    pub h_source: String,
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Flavor {
    ElfProcess,
    // TODO(https://fxbug.dev/108880) delete once unified FIDL available OOT
    ElfHlcpp,
    Driver,
}

#[derive(Debug, thiserror::Error)]
pub enum FlavorParseError {
    #[error("Unknown flavor '{_0}', expected 'elf' or 'driver'")]
    UnknownFlavor(String),
}

impl FromStr for Flavor {
    type Err = FlavorParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let string = s.to_ascii_lowercase();

        match string.as_str() {
            "elf" => Ok(Flavor::ElfProcess),
            "elf-hlcpp" => Ok(Flavor::ElfHlcpp),
            "driver" => Ok(Flavor::Driver),
            _ => Err(FlavorParseError::UnknownFlavor(string)),
        }
    }
}

pub fn create_cpp_wrapper(
    config_decl: &ConfigDecl,
    cpp_namespace: String,
    fidl_library_name: String,
    flavor: Flavor,
) -> Result<CppSource, SourceGenError> {
    let (cc_source_template, h_source_template) = match flavor {
        Flavor::ElfProcess => (CC_ELF_SOURCE_TEMPLATE, H_ELF_SOURCE_TEMPLATE),
        Flavor::ElfHlcpp => (CC_ELF_HLCPP_SOURCE_TEMPLATE, H_ELF_SOURCE_TEMPLATE),
        Flavor::Driver => (CC_DRIVER_SOURCE_TEMPLATE, H_DRIVER_SOURCE_TEMPLATE),
    };

    let vars = TemplateVars::from_decl(config_decl, cpp_namespace, fidl_library_name, flavor);

    let mut hbars = Handlebars::new();
    hbars.set_strict_mode(true);
    hbars.register_escape_fn(handlebars::no_escape);
    hbars.register_helper("hex_byte", Box::new(hex_byte));
    hbars.register_helper("is_string_vector", Box::new(is_string_vector));
    hbars.register_helper("is_bool", Box::new(is_bool));
    hbars.register_helper("is_vector", Box::new(is_vector));
    hbars.register_helper("is_string", Box::new(is_string));
    hbars.register_helper("cpp_type", Box::new(cpp_type));
    hbars.register_helper("inspect_type", Box::new(inspect_type));
    hbars.register_template_string("cc_source", cc_source_template).pretty_unwrap();
    hbars.register_template_string("h_source", h_source_template).pretty_unwrap();
    hbars.register_template_string("helpers", HELPERS_SOURCE_TEMPLATE).pretty_unwrap();
    hbars.register_template_string("typedef", TYPEDEF_SOURCE_TEMPLATE).pretty_unwrap();
    hbars.register_template_string("vmo_parse", VMO_PARSE_SOURCE_TEMPLATE).pretty_unwrap();
    hbars
        .register_template_string("vmo_parse_helpers", VMO_PARSE_HELPERS_SOURCE_TEMPLATE)
        .pretty_unwrap();
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

    // TODO(https://fxbug.dev/108880) delete once unified FIDL available OOT
    fidl_cpp_header_prefix: String,
}

impl TemplateVars {
    fn from_decl(
        config_decl: &ConfigDecl,
        cpp_namespace: String,
        fidl_library_name: String,
        flavor: Flavor,
    ) -> Self {
        let cpp_namespace = cpp_namespace.replace('.', "_").replace('-', "_").to_ascii_lowercase();
        let header_guard = fidl_library_name.replace('.', "_").to_ascii_uppercase();
        let (fidl_cpp_namespace, fidl_cpp_header_prefix) = if let Flavor::ElfHlcpp = flavor {
            (
                fidl_library_name.replace('.', "::").to_ascii_lowercase(),
                fidl_library_name.replace('.', "/").to_ascii_lowercase(),
            )
        } else {
            let ns = fidl_library_name.replace('.', "_").to_ascii_lowercase();
            (ns.clone(), ns)
        };
        let ConfigChecksum::Sha256(expected_checksum) = &config_decl.checksum;
        let expected_checksum = expected_checksum.to_vec();

        Self {
            fields: config_decl.fields.iter().map(Field::from_decl).collect(),
            header_guard,
            fidl_library_name,
            fidl_cpp_namespace,
            fidl_cpp_header_prefix,
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
handlebars_helper!(is_bool: |t: ConfigValueType| matches!(t, ConfigValueType::Bool { .. }));
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
