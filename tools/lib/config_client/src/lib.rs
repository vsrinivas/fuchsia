// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library for generating structured configuration accessors. Each generated
//! language-specific library depends on the output of [`create_fidl_source`].

use cm_rust::{
    ConfigDecl, ConfigField, ConfigStringType, ConfigValueType, ConfigVectorElementType,
    ConfigVectorType,
};
use proc_macro2::{Ident, Literal};
use quote::quote;
use std::str::FromStr;
use syn::{parse_str, Error as SynError};
use thiserror::Error;

/// Create a custom FIDL library source file containing all the fields of a config declaration
pub fn create_fidl_source(library_name: &str, config_decl: &ConfigDecl) -> String {
    // TODO(http://fxbug.dev/90685): Add additional verification of library name for FIDL
    // source files
    let mut fidl_struct_fields = vec![];
    for ConfigField { key, value_type } in &config_decl.fields {
        let fidl_type = config_value_type_to_fidl_type(&value_type);
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
    // TODO(http://fxbug.dev/90692): Create a new structure for the config fields instead of
    // re-exporting the FIDL Config struct.
    let library_name = parse_str::<Ident>(&format!("fidl_{}", library_name)).map_err(|source| {
        SourceGenError::InvalidLibraryName { name: library_name.to_string(), source }
    })?;
    let expected_checksum = &config_decl.declaration_checksum;

    let expected_checksum =
        expected_checksum.into_iter().map(|b| Literal::from_str(&format!("{:#04x}", b)).unwrap());

    let stream = quote! {
        pub use #library_name::Config;
        use fidl::encoding::decode_persistent;
        use fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType};
        use fuchsia_zircon as zx;

        pub fn get_config() -> Config {
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

            decode_persistent(&config_bytes[fidl_start..]).expect("must be able to parse bytes as config FIDL")
        }
    };

    Ok(stream.to_string())
}

/// Error from generating a source file
#[derive(Debug, Error)]
pub enum SourceGenError {
    #[error("The given library name `{name}` is not a valid Rust identifier")]
    InvalidLibraryName { name: String, source: SynError },
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
            pub use fidl_testcomponent::Config;
            use fidl::encoding::decode_persistent;
            use fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType};
            use fuchsia_zircon as zx;

            pub fn get_config() -> Config {
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

                decode_persistent(&config_bytes[fidl_start..]).expect("must be able to parse bytes as config FIDL")
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
