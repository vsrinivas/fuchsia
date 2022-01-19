// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library for generating structured configuration accessors. Each generated
//! language-specific library depends on the output of [`create_fidl_source`].

use cm_rust::{
    ConfigDecl, ConfigField, ConfigStringType, ConfigValueType, ConfigVectorElementType,
    ConfigVectorType,
};
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
pub fn create_fidl_source(library_name: &str, config_decl: &ConfigDecl) -> String {
    // TODO(http://fxbug.dev/90685): Add additional verification of library name for FIDL
    // source files
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
    library_name: &str,
    config_decl: &ConfigDecl,
) -> Result<String, SourceGenError> {
    let library_name = parse_str::<Ident>(&format!("fidl_{}", library_name)).map_err(|source| {
        SourceGenError::InvalidIdentifier { input: library_name.to_string(), source }
    })?;
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
        use #library_name::Config as FidlConfig;
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
        ConfigValueType::Bool(_) => "bool".to_string(),
        ConfigValueType::Uint8(_) => "uint8".to_string(),
        ConfigValueType::Uint16(_) => "uint16".to_string(),
        ConfigValueType::Uint32(_) => "uint32".to_string(),
        ConfigValueType::Uint64(_) => "uint64".to_string(),
        ConfigValueType::Int8(_) => "int8".to_string(),
        ConfigValueType::Int16(_) => "int16".to_string(),
        ConfigValueType::Int32(_) => "int32".to_string(),
        ConfigValueType::Int64(_) => "int64".to_string(),
        ConfigValueType::String(ConfigStringType { max_size }) => format!("string:{}", max_size),
        ConfigValueType::Vector(ConfigVectorType { max_count, element_type }) => format!(
            "vector<{}>:{}",
            config_vector_element_type_to_fidl_type(element_type),
            max_count
        ),
    }
}

fn config_vector_element_type_to_fidl_type(element_type: &ConfigVectorElementType) -> String {
    match element_type {
        ConfigVectorElementType::Bool(_) => "bool".to_string(),
        ConfigVectorElementType::Uint8(_) => "uint8".to_string(),
        ConfigVectorElementType::Uint16(_) => "uint16".to_string(),
        ConfigVectorElementType::Uint32(_) => "uint32".to_string(),
        ConfigVectorElementType::Uint64(_) => "uint64".to_string(),
        ConfigVectorElementType::Int8(_) => "int8".to_string(),
        ConfigVectorElementType::Int16(_) => "int16".to_string(),
        ConfigVectorElementType::Int32(_) => "int32".to_string(),
        ConfigVectorElementType::Int64(_) => "int64".to_string(),
        ConfigVectorElementType::String(ConfigStringType { max_size }) => {
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
        ConfigValueType::Bool(_) => quote! {
            pub #field: bool
        },
        ConfigValueType::Uint8(_) => quote! {
            pub #field: u8
        },
        ConfigValueType::Uint16(_) => quote! {
            pub #field: u16
        },
        ConfigValueType::Uint32(_) => quote! {
            pub #field: u32
        },
        ConfigValueType::Uint64(_) => quote! {
            pub #field: u64
        },
        ConfigValueType::Int8(_) => quote! {
            pub #field: i8
        },
        ConfigValueType::Int16(_) => quote! {
            pub #field: i16
        },
        ConfigValueType::Int32(_) => quote! {
            pub #field: i32
        },
        ConfigValueType::Int64(_) => quote! {
            pub #field: i64
        },
        ConfigValueType::String(_) => quote! {
            pub #field: String
        },
        ConfigValueType::Vector(ConfigVectorType { element_type, .. }) => match element_type {
            ConfigVectorElementType::Bool(_) => quote! {
                pub #field: Vec<bool>
            },
            ConfigVectorElementType::Uint8(_) => quote! {
                pub #field: Vec<u8>
            },
            ConfigVectorElementType::Uint16(_) => quote! {
                pub #field: Vec<u16>
            },
            ConfigVectorElementType::Uint32(_) => quote! {
                pub #field: Vec<u32>
            },
            ConfigVectorElementType::Uint64(_) => quote! {
                pub #field: Vec<u64>
            },
            ConfigVectorElementType::Int8(_) => quote! {
                pub #field: Vec<i8>
            },
            ConfigVectorElementType::Int16(_) => quote! {
                pub #field: Vec<i16>
            },
            ConfigVectorElementType::Int32(_) => quote! {
                pub #field: Vec<i32>
            },
            ConfigVectorElementType::Int64(_) => quote! {
                pub #field: Vec<i64>
            },
            ConfigVectorElementType::String(_) => quote! {
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

        let observed_fidl_src = create_fidl_source("testcomponent", &decl);
        let expected_fidl_src = "library testcomponent;
type Config = struct {
my_flag bool;
my_uint8 uint8;
my_uint16 uint16;
my_uint32 uint32;
my_uint64 uint64;
my_int8 int8;
my_int16 int16;
my_int32 int32;
my_int64 int64;
my_string string:100;
my_vector_of_flag vector<bool>:100;
my_vector_of_uint8 vector<uint8>:100;
my_vector_of_uint16 vector<uint16>:100;
my_vector_of_uint32 vector<uint32>:100;
my_vector_of_uint64 vector<uint64>:100;
my_vector_of_int8 vector<int8>:100;
my_vector_of_int16 vector<int16>:100;
my_vector_of_int32 vector<int32>:100;
my_vector_of_int64 vector<int64>:100;
my_vector_of_string vector<string:100>:100;
};";
        assert_eq!(observed_fidl_src, expected_fidl_src);

        let actual_rust_src = create_rust_wrapper("testcomponent", &decl).unwrap();

        let expected_rust_src = quote! {
            use fidl_testcomponent::Config as FidlConfig;
            use fidl::encoding::decode_persistent;
            use fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType};
            use fuchsia_zircon as zx;

            pub struct Config {
                pub my_flag: bool,
                pub my_uint8: u8,
                pub my_uint16: u16,
                pub my_uint32: u32,
                pub my_uint64: u64,
                pub my_int8: i8,
                pub my_int16: i16,
                pub my_int32: i32,
                pub my_int64: i64,
                pub my_string: String,
                pub my_vector_of_flag: Vec<bool>,
                pub my_vector_of_uint8: Vec<u8>,
                pub my_vector_of_uint16: Vec<u16>,
                pub my_vector_of_uint32: Vec<u32>,
                pub my_vector_of_uint64: Vec<u64>,
                pub my_vector_of_int8: Vec<i8>,
                pub my_vector_of_int16: Vec<i16>,
                pub my_vector_of_int32: Vec<i32>,
                pub my_vector_of_int64: Vec<i64>,
                pub my_vector_of_string: Vec<String>
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
                        my_flag: fidl_config.my_flag,
                        my_uint8: fidl_config.my_uint8,
                        my_uint16: fidl_config.my_uint16,
                        my_uint32: fidl_config.my_uint32,
                        my_uint64: fidl_config.my_uint64,
                        my_int8: fidl_config.my_int8,
                        my_int16: fidl_config.my_int16,
                        my_int32: fidl_config.my_int32,
                        my_int64: fidl_config.my_int64,
                        my_string: fidl_config.my_string,
                        my_vector_of_flag: fidl_config.my_vector_of_flag,
                        my_vector_of_uint8: fidl_config.my_vector_of_uint8,
                        my_vector_of_uint16: fidl_config.my_vector_of_uint16,
                        my_vector_of_uint32: fidl_config.my_vector_of_uint32,
                        my_vector_of_uint64: fidl_config.my_vector_of_uint64,
                        my_vector_of_int8: fidl_config.my_vector_of_int8,
                        my_vector_of_int16: fidl_config.my_vector_of_int16,
                        my_vector_of_int32: fidl_config.my_vector_of_int32,
                        my_vector_of_int64: fidl_config.my_vector_of_int64,
                        my_vector_of_string: fidl_config.my_vector_of_string
                    }
                }
            }
        }.to_string();

        assert_eq!(actual_rust_src, expected_rust_src);
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

        let observed_fidl_src = create_fidl_source("testcomponent", &decl);
        let expected_fidl_src = "library testcomponent;
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

        let actual_rust_src = create_rust_wrapper("testcomponent", &decl).unwrap();

        let expected_rust_src = quote! {
            use fidl_testcomponent::Config as FidlConfig;
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

    #[test]
    fn bad_library_name() {
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

        create_rust_wrapper("bad.library.name", &decl)
            .expect_err("Rust source compilation accepted bad identifier for library name");

        create_rust_wrapper("bad-library-name", &decl)
            .expect_err("Rust source compilation accepted bad identifier for library name");

        create_rust_wrapper("bad+library+name", &decl)
            .expect_err("Rust source compilation accepted bad identifier for library name");
    }
}
