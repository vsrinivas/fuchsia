// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library for generating structured configuration accessors. Each generated
//! language-specific library depends on the output of [`create_fidl_source`].

use cm_rust::{ConfigDecl, ConfigField, ConfigNestedValueType, ConfigValueType};
use proc_macro2::{Ident, Literal, TokenStream};
use quote::quote;
use std::str::FromStr;
use syn::{parse_str, Error as SynError};
use thiserror::Error;

// TODO(http://fxbug.dev/91666): This list should be kept in sync with fidlgen_rust.
const RESERVED_SUFFIXES: [&str; 7] =
    ["Impl", "Marker", "Proxy", "ProxyProtocol", "ControlHandle", "Responder", "Server"];

// TODO(http://fxbug.dev/91666): This list should be kept in sync with fidlgen_rust.
const RESERVED_WORDS: [&str; 73] = [
    "as",
    "box",
    "break",
    "const",
    "continue",
    "crate",
    "else",
    "enum",
    "extern",
    "false",
    "fn",
    "for",
    "if",
    "impl",
    "in",
    "let",
    "loop",
    "match",
    "mod",
    "move",
    "mut",
    "pub",
    "ref",
    "return",
    "self",
    "Self",
    "static",
    "struct",
    "super",
    "trait",
    "true",
    "type",
    "unsafe",
    "use",
    "where",
    "while",
    // Keywords reserved for future use (future-proofing...)
    "abstract",
    "alignof",
    "await",
    "become",
    "do",
    "final",
    "macro",
    "offsetof",
    "override",
    "priv",
    "proc",
    "pure",
    "sizeof",
    "typeof",
    "unsized",
    "virtual",
    "yield",
    // Weak keywords (special meaning in specific contexts)
    // These are ok in all contexts of fidl names.
    //"default",
    //"union",

    // Things that are not keywords, but for which collisions would be very unpleasant
    "Result",
    "Ok",
    "Err",
    "Vec",
    "Option",
    "Some",
    "None",
    "Box",
    "Future",
    "Stream",
    "Never",
    "Send",
    "fidl",
    "futures",
    "zx",
    "async",
    "on_open",
    "OnOpen",
    // TODO(fxbug.dev/66767): Remove "WaitForEvent".
    "wait_for_event",
    "WaitForEvent",
];

/// Create a custom FIDL library source file containing all the fields of a config declaration
pub fn create_fidl_source(config_decl: &ConfigDecl, library_name: String) -> String {
    let mut fidl_struct_fields = vec![];
    for ConfigField { key, value_type } in &config_decl.fields {
        let fidl_type = config_value_type_to_fidl_type(&value_type);
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

/// Create a Rust wrapper file containing all the fields of a config declaration
pub fn create_rust_wrapper(
    config_decl: &ConfigDecl,
    fidl_library_name: String,
) -> Result<String, SourceGenError> {
    let fidl_library_name =
        format!("fidl_{}", fidl_library_name.replace('.', "_").to_ascii_lowercase());
    let fidl_library_name = parse_str::<Ident>(&fidl_library_name)
        .map_err(|source| SourceGenError::InvalidIdentifier { input: fidl_library_name, source })?;
    let expected_checksum = &config_decl.declaration_checksum;

    let expected_checksum =
        expected_checksum.into_iter().map(|b| Literal::from_str(&format!("{:#04x}", b)).unwrap());

    let mut field_declarations = vec![];
    let mut field_conversions = vec![];

    for field in &config_decl.fields {
        let (decl, conversion) =
            get_rust_field_declaration_and_conversion(&field.key, &field.value_type)?;
        field_declarations.push(decl);
        field_conversions.push(conversion)
    }

    let stream = quote! {
        use #fidl_library_name::Config as FidlConfig;
        use fidl::encoding::decode_persistent;
        use fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType};
        use fuchsia_zircon as zx;

        pub struct Config {
            #(#field_declarations),*
        }

        impl Config {
            pub fn from_args() -> Self {
                let config_vmo: zx::Vmo = take_startup_handle(HandleInfo::new(HandleType::ConfigVmo, 0))
                    .expect("must have been provided with a config vmo")
                    .into();
                let config_size = config_vmo.get_content_size().expect("must be able to read config vmo content size");
                assert_ne!(config_size, 0, "config vmo must be non-empty");

                let mut config_bytes = Vec::new();
                config_bytes.resize(config_size as usize, 0);
                config_vmo.read(&mut config_bytes, 0).expect("must be able to read config vmo");

                let checksum_length = u16::from_le_bytes([config_bytes[0], config_bytes[1]]) as usize;
                let fidl_start = 2 + checksum_length;
                let observed_checksum = &config_bytes[2..fidl_start];
                let expected_checksum = vec![#(#expected_checksum),*];

                assert_eq!(observed_checksum, expected_checksum, "checksum from config VMO does not match expected checksum");

                let fidl_config: FidlConfig = decode_persistent(&config_bytes[fidl_start..]).expect("must be able to parse bytes as config FIDL");

                Self {
                    #(#field_conversions),*
                }
            }
        }
    };

    Ok(stream.to_string())
}

/// Error from generating a source file
#[derive(Debug, Error)]
pub enum SourceGenError {
    #[error("The given string `{input}` is not a valid Rust identifier")]
    InvalidIdentifier { input: String, source: SynError },
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

// TODO(http://fxbug.dev/91666): This logic should be kept in sync with fidlgen_rust.
fn normalize_field_key(key: &str) -> String {
    let mut identifier = String::new();
    let mut saw_lowercase_or_digit = false;
    for c in key.chars() {
        if c.is_ascii_uppercase() {
            if saw_lowercase_or_digit {
                // A lowercase letter or digit preceded this uppercase letter.
                // Break this into two words.
                identifier.push('_');
                saw_lowercase_or_digit = false;
            }
            identifier.push(c.to_ascii_lowercase());
        } else if c == '_' {
            identifier.push('_');
            saw_lowercase_or_digit = false;
        } else {
            identifier.push(c);
            saw_lowercase_or_digit = true;
        }
    }

    if RESERVED_WORDS.contains(&key) || RESERVED_SUFFIXES.iter().any(|s| key.starts_with(s)) {
        identifier.push('_')
    }

    identifier
}

fn get_rust_field_declaration_and_conversion(
    key: &str,
    value_type: &ConfigValueType,
) -> Result<(TokenStream, TokenStream), SourceGenError> {
    let identifier = normalize_field_key(key);
    let field = parse_str::<Ident>(&identifier)
        .map_err(|source| SourceGenError::InvalidIdentifier { input: key.to_string(), source })?;
    let decl = match value_type {
        ConfigValueType::Bool => quote! {
            pub #field: bool
        },
        ConfigValueType::Uint8 => quote! {
            pub #field: u8
        },
        ConfigValueType::Uint16 => quote! {
            pub #field: u16
        },
        ConfigValueType::Uint32 => quote! {
            pub #field: u32
        },
        ConfigValueType::Uint64 => quote! {
            pub #field: u64
        },
        ConfigValueType::Int8 => quote! {
            pub #field: i8
        },
        ConfigValueType::Int16 => quote! {
            pub #field: i16
        },
        ConfigValueType::Int32 => quote! {
            pub #field: i32
        },
        ConfigValueType::Int64 => quote! {
            pub #field: i64
        },
        ConfigValueType::String { .. } => quote! {
            pub #field: String
        },
        ConfigValueType::Vector { nested_type, .. } => match nested_type {
            ConfigNestedValueType::Bool => quote! {
                pub #field: Vec<bool>
            },
            ConfigNestedValueType::Uint8 => quote! {
                pub #field: Vec<u8>
            },
            ConfigNestedValueType::Uint16 => quote! {
                pub #field: Vec<u16>
            },
            ConfigNestedValueType::Uint32 => quote! {
                pub #field: Vec<u32>
            },
            ConfigNestedValueType::Uint64 => quote! {
                pub #field: Vec<u64>
            },
            ConfigNestedValueType::Int8 => quote! {
                pub #field: Vec<i8>
            },
            ConfigNestedValueType::Int16 => quote! {
                pub #field: Vec<i16>
            },
            ConfigNestedValueType::Int32 => quote! {
                pub #field: Vec<i32>
            },
            ConfigNestedValueType::Int64 => quote! {
                pub #field: Vec<i64>
            },
            ConfigNestedValueType::String { .. } => quote! {
                pub #field: Vec<String>
            },
        },
    };
    let conversion = quote! {
        #field: fidl_config.#field
    };
    Ok((decl, conversion))
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_component_config_ext::config_decl;

    fn test_checksum() -> Vec<u8> {
        // sha256("Back to the Fuchsia")
        vec![
            0xb5, 0xf9, 0x33, 0xe8, 0x94, 0x56, 0x3a, 0xf9, 0x61, 0x39, 0xe5, 0x05, 0x79, 0x4b,
            0x88, 0xa5, 0x3e, 0xd4, 0xd1, 0x5c, 0x32, 0xe2, 0xb4, 0x49, 0x9e, 0x42, 0xeb, 0xa3,
            0x32, 0xb1, 0xf5, 0xbb,
        ]
    }

    #[test]
    fn bad_field_names() {
        let decl = config_decl! {
            ck@ test_checksum(),
            snake_case_string: { bool },
            lowerCamelCaseString: { bool },
            UpperCamelCaseString: { bool },
            CONST_CASE: { bool },
            stringThatHas02Digits: { bool },
            mixedLowerCamel_snakeCaseString: { bool },
            MixedUpperCamel_SnakeCaseString: { bool },
            multiple__underscores: { bool },
            unsafe: { bool },
            ServerMode: { bool },
        };

        let observed_fidl_src = create_fidl_source(&decl, "cf.sc.internal".to_string());
        let expected_fidl_src = "library cf.sc.internal;
type Config = struct {
snake_case_string bool;
lower_camel_case_string bool;
upper_camel_case_string bool;
const_case bool;
string_that_has02_digits bool;
mixed_lower_camel_snake_case_string bool;
mixed_upper_camel_snake_case_string bool;
multiple__underscores bool;
unsafe_ bool;
server_mode_ bool;
};";
        assert_eq!(observed_fidl_src, expected_fidl_src);

        let actual_rust_src = create_rust_wrapper(&decl, "cf.sc.internal".to_string()).unwrap();

        let expected_rust_src = quote! {
            use fidl_cf_sc_internal::Config as FidlConfig;
            use fidl::encoding::decode_persistent;
            use fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType};
            use fuchsia_zircon as zx;

            pub struct Config {
                pub snake_case_string: bool,
                pub lower_camel_case_string: bool,
                pub upper_camel_case_string: bool,
                pub const_case: bool,
                pub string_that_has02_digits: bool,
                pub mixed_lower_camel_snake_case_string: bool,
                pub mixed_upper_camel_snake_case_string: bool,
                pub multiple__underscores: bool,
                pub unsafe_: bool,
                pub server_mode_: bool
            }

            impl Config {
                pub fn from_args() -> Self {
                    let config_vmo: zx::Vmo = take_startup_handle(HandleInfo::new(HandleType::ConfigVmo, 0))
                        .expect("must have been provided with a config vmo")
                        .into();
                    let config_size = config_vmo.get_content_size().expect("must be able to read config vmo content size");
                    assert_ne!(config_size, 0, "config vmo must be non-empty");

                    let mut config_bytes = Vec::new();
                    config_bytes.resize(config_size as usize, 0);
                    config_vmo.read(&mut config_bytes, 0).expect("must be able to read config vmo");

                    let checksum_length = u16::from_le_bytes([config_bytes[0], config_bytes[1]]) as usize;
                    let fidl_start = 2 + checksum_length;
                    let observed_checksum = &config_bytes[2..fidl_start];
                    let expected_checksum = vec![
                        0xb5, 0xf9, 0x33, 0xe8, 0x94, 0x56, 0x3a, 0xf9, 0x61, 0x39, 0xe5, 0x05, 0x79,
                        0x4b, 0x88, 0xa5, 0x3e, 0xd4, 0xd1, 0x5c, 0x32, 0xe2, 0xb4, 0x49, 0x9e, 0x42,
                        0xeb, 0xa3, 0x32, 0xb1, 0xf5, 0xbb
                    ];

                    assert_eq!(observed_checksum, expected_checksum, "checksum from config VMO does not match expected checksum");

                    let fidl_config: FidlConfig = decode_persistent(&config_bytes[fidl_start..]).expect("must be able to parse bytes as config FIDL");

                    Self {
                        snake_case_string: fidl_config.snake_case_string,
                        lower_camel_case_string: fidl_config.lower_camel_case_string,
                        upper_camel_case_string: fidl_config.upper_camel_case_string,
                        const_case: fidl_config.const_case,
                        string_that_has02_digits: fidl_config.string_that_has02_digits,
                        mixed_lower_camel_snake_case_string: fidl_config.mixed_lower_camel_snake_case_string,
                        mixed_upper_camel_snake_case_string: fidl_config.mixed_upper_camel_snake_case_string,
                        multiple__underscores: fidl_config.multiple__underscores,
                        unsafe_: fidl_config.unsafe_,
                        server_mode_: fidl_config.server_mode_
                    }
                }
            }
        }.to_string();

        assert_eq!(actual_rust_src, expected_rust_src);
    }
}
