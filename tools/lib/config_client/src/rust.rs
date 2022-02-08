// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{normalize_field_key, SourceGenError};
use cm_rust::{ConfigChecksum, ConfigDecl, ConfigNestedValueType, ConfigValueType};
use proc_macro2::{Ident, Literal, TokenStream};
use quote::quote;
use std::str::FromStr;
use syn::parse_str;

/// Create a Rust wrapper file containing all the fields of a config declaration
pub fn create_rust_wrapper(
    config_decl: &ConfigDecl,
    fidl_library_name: String,
) -> Result<String, SourceGenError> {
    let fidl_library_name =
        format!("fidl_{}", fidl_library_name.replace('.', "_").to_ascii_lowercase());
    let fidl_library_name = parse_str::<Ident>(&fidl_library_name)
        .map_err(|source| SourceGenError::InvalidIdentifier { input: fidl_library_name, source })?;
    let ConfigChecksum::Sha256(expected_checksum) = &config_decl.checksum;

    let expected_checksum =
        expected_checksum.into_iter().map(|b| Literal::from_str(&format!("{:#04x}", b)).unwrap());

    let mut field_declarations = vec![];
    let mut field_conversions = vec![];

    let mut record_to_inspect_ops = vec![];
    let mut inspect_uses = vec![quote!(Node)];

    let mut needs_array_property = false;
    let mut needs_arithmetic_array_property = false;

    for field in &config_decl.fields {
        let RustTokens { decl, conversion, record_to_inspect } =
            get_rust_tokens(&field.key, &field.type_)?;

        if let ConfigValueType::Vector {
            nested_type: ConfigNestedValueType::String { .. }, ..
        } = &field.type_
        {
            needs_array_property = true;
        } else if let ConfigValueType::Vector { .. } = &field.type_ {
            needs_arithmetic_array_property = true;
        }

        record_to_inspect_ops.push(record_to_inspect);
        field_declarations.push(decl);
        field_conversions.push(conversion)
    }

    if needs_arithmetic_array_property {
        inspect_uses.push(quote!(ArithmeticArrayProperty))
    }
    if needs_array_property {
        inspect_uses.push(quote!(ArrayProperty))
    }

    let stream = quote! {
        use #fidl_library_name::Config as FidlConfig;
        use fidl::encoding::decode_persistent;
        use fuchsia_inspect::{#(#inspect_uses),*};
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

            pub fn record_to_inspect(self, root_node: &Node)->Self{
                root_node.record_child("config", |inspector_node| {
                    #(#record_to_inspect_ops)*
                });
                self
            }
        }
    };

    Ok(stream.to_string())
}

struct RustTokens {
    decl: TokenStream,
    conversion: TokenStream,
    record_to_inspect: TokenStream,
}

fn get_rust_tokens(key: &str, value_type: &ConfigValueType) -> Result<RustTokens, SourceGenError> {
    let identifier = normalize_field_key(key);
    let field = parse_str::<Ident>(&identifier)
        .map_err(|source| SourceGenError::InvalidIdentifier { input: key.to_string(), source })?;

    let (record_to_inspect, decl) = match value_type {
        ConfigValueType::Bool => (
            quote! {
                inspector_node.record_bool(#key, self.#field);
            },
            quote! {
                pub #field: bool
            },
        ),
        ConfigValueType::Uint8 => (
            quote! {
                inspector_node.record_uint(#key, self.#field as u64);
            },
            quote! {
                pub #field: u8
            },
        ),
        ConfigValueType::Uint16 => (
            quote! {
                inspector_node.record_uint(#key, self.#field as u64);
            },
            quote! {
                pub #field: u16
            },
        ),
        ConfigValueType::Uint32 => (
            quote! {
                inspector_node.record_uint(#key, self.#field as u64);
            },
            quote! {
                pub #field: u32
            },
        ),
        ConfigValueType::Uint64 => (
            quote! {
                inspector_node.record_uint(#key, self.#field);
            },
            quote! {
                pub #field: u64
            },
        ),
        ConfigValueType::Int8 => (
            quote! {
                inspector_node.record_int(#key, self.#field as i64);
            },
            quote! {
                 pub #field: i8
            },
        ),
        ConfigValueType::Int16 => (
            quote! {
                inspector_node.record_int(#key, self.#field as i64);
            },
            quote! {
                 pub #field: i16
            },
        ),
        ConfigValueType::Int32 => (
            quote! {
                inspector_node.record_int(#key, self.#field as i64);
            },
            quote! {
                 pub #field: i32
            },
        ),
        ConfigValueType::Int64 => (
            quote! {
                inspector_node.record_int(#key, self.#field);
            },
            quote! {
                 pub #field: i64
            },
        ),
        ConfigValueType::String { .. } => (
            quote! {
                inspector_node.record_string(#key, &self.#field);
            },
            quote! {
                 pub #field: String
            },
        ),
        ConfigValueType::Vector { nested_type, .. } => match nested_type {
            ConfigNestedValueType::Bool => (
                quote! {
                    let arr = inspector_node.create_uint_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i] as u64);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                     pub #field: Vec<bool>
                },
            ),
            ConfigNestedValueType::Uint8 => (
                quote! {
                    let arr = inspector_node.create_uint_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i] as u64);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                     pub #field: Vec<u8>
                },
            ),
            ConfigNestedValueType::Uint16 => (
                quote! {
                    let arr = inspector_node.create_uint_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i] as u64);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                     pub #field: Vec<u16>
                },
            ),
            ConfigNestedValueType::Uint32 => (
                quote! {
                    let arr = inspector_node.create_uint_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i] as u64);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                     pub #field: Vec<u32>
                },
            ),
            ConfigNestedValueType::Uint64 => (
                quote! {
                    let arr = inspector_node.create_uint_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i]);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                     pub #field: Vec<u64>
                },
            ),
            ConfigNestedValueType::Int8 => (
                quote! {
                    let arr = inspector_node.create_int_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i] as i64);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                     pub #field: Vec<i8>
                },
            ),
            ConfigNestedValueType::Int16 => (
                quote! {
                    let arr = inspector_node.create_int_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i] as i64);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                     pub #field: Vec<i16>
                },
            ),
            ConfigNestedValueType::Int32 => (
                quote! {
                    let arr = inspector_node.create_int_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i] as i64);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                    pub #field: Vec<i32>
                },
            ),
            ConfigNestedValueType::Int64 => (
                quote! {
                    let arr = inspector_node.create_int_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.add(i, self.#field[i]);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                    pub #field: Vec<i64>
                },
            ),
            ConfigNestedValueType::String { .. } => (
                quote! {
                    let arr = inspector_node.create_string_array(#key, self.#field.len());
                    for i in 0..self.#field.len() {
                        arr.set(i, &self.#field[i]);
                    }
                    inspector_node.record(arr);
                },
                quote! {
                    pub #field: Vec<String>
                },
            ),
        },
    };
    let conversion = quote! {
        #field: fidl_config.#field
    };
    Ok(RustTokens { decl, conversion, record_to_inspect })
}
