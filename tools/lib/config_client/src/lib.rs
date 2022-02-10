// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library for generating structured configuration accessors. Each generated
//! language-specific library depends on the output of [`create_fidl_source`].

pub mod cpp_elf;
pub mod fidl;
pub mod rust;

use syn::Error as SynError;
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

/// Error from generating a source file
#[derive(Debug, Error)]
pub enum SourceGenError {
    #[error("The given string `{input}` is not a valid Rust identifier")]
    InvalidIdentifier { input: String, source: SynError },
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

#[cfg(test)]
mod tests {
    use super::*;
    use cm_rust::ConfigChecksum;
    use fidl_fuchsia_component_config_ext::config_decl;
    use quote::quote;

    fn test_checksum() -> ConfigChecksum {
        // sha256("Back to the Fuchsia")
        ConfigChecksum::Sha256([
            0xb5, 0xf9, 0x33, 0xe8, 0x94, 0x56, 0x3a, 0xf9, 0x61, 0x39, 0xe5, 0x05, 0x79, 0x4b,
            0x88, 0xa5, 0x3e, 0xd4, 0xd1, 0x5c, 0x32, 0xe2, 0xb4, 0x49, 0x9e, 0x42, 0xeb, 0xa3,
            0x32, 0xb1, 0xf5, 0xbb,
        ])
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

        let observed_fidl_src = fidl::create_fidl_source(&decl, "cf.sc.internal".to_string());
        let expected_fidl_src = "
library cf.sc.internal;

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
};
";
        assert_eq!(observed_fidl_src, expected_fidl_src);

        let actual_rust_src =
            rust::create_rust_wrapper(&decl, "cf.sc.internal".to_string()).unwrap();

        let expected_rust_src = quote! {
            use fidl_cf_sc_internal::Config as FidlConfig;
            use fidl::encoding::decode_persistent;
            use fuchsia_inspect::{Node};
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
               pub fn record_to_inspect(self, root_node : & Node) -> Self {
                    root_node.record_child("config", |inspector_node| {
                        inspector_node.record_bool("snake_case_string", self.snake_case_string);
                        inspector_node.record_bool("lowerCamelCaseString", self.lower_camel_case_string);
                        inspector_node.record_bool("UpperCamelCaseString", self.upper_camel_case_string);
                        inspector_node.record_bool("CONST_CASE", self.const_case);
                        inspector_node.record_bool("stringThatHas02Digits", self.string_that_has02_digits);
                        inspector_node.record_bool("mixedLowerCamel_snakeCaseString", self.mixed_lower_camel_snake_case_string);
                        inspector_node.record_bool("MixedUpperCamel_SnakeCaseString", self.mixed_upper_camel_snake_case_string);
                        inspector_node.record_bool("multiple__underscores", self.multiple__underscores);
                        inspector_node.record_bool("unsafe", self.unsafe_);
                        inspector_node.record_bool("ServerMode", self.server_mode_);
                    });
                    self
                }
            }
        }.to_string();

        assert_eq!(actual_rust_src, expected_rust_src);

        let actual_cpp_elf_src = cpp_elf::create_cpp_elf_wrapper(
            &decl,
            "cf_sc_internal".into(),
            "cf.sc.internal".into(),
        )
        .unwrap();
        let expected_cpp_elf_src_h = r#"#ifndef __CF_SC_INTERNAL_H__
#define __CF_SC_INTERNAL_H__

#include <string>
#include <vector>

#include <lib/sys/inspect/cpp/component.h>

namespace cf_sc_internal {
struct Config {
    bool snake_case_string;
    bool lower_camel_case_string;
    bool upper_camel_case_string;
    bool const_case;
    bool string_that_has02_digits;
    bool mixed_lower_camel_snake_case_string;
    bool mixed_upper_camel_snake_case_string;
    bool multiple__underscores;
    bool unsafe_;
    bool server_mode_;

  static Config from_args() noexcept;

  void record_to_inspect(sys::ComponentInspector * inspector);
};
}

#endif
"#;
        let expected_cpp_elf_src_cc = r#"#include <fidl/cf.sc.internal/cpp/wire.h>
#include <fidl/cf.sc.internal/cpp/wire_types.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <string>
#include <vector>

#include "config.h"

template <class T>
std::vector<T> from_vector_view(fidl::VectorView<T> v) {
    size_t count = v.count();
    std::vector<T> data(count);
    for (size_t i = 0; i < count; i++) {
        data[i] = v[i];
    }
    return data;
}

std::vector<std::string> from_vector_string_view(fidl::VectorView<fidl::StringView> v) {
    size_t count = v.count();
    std::vector<std::string> data(count);
    for (size_t i = 0; i < count; i++) {
        data[i] = std::string(v[i].get());
    }
    return data;
}

cf_sc_internal::Config cf_sc_internal::Config::from_args() noexcept {
  // Get the VMO containing FIDL config
  zx_handle_t config_vmo_handle = zx_take_startup_handle(PA_CONFIG_VMO);
  ZX_ASSERT_MSG(config_vmo_handle != ZX_HANDLE_INVALID, "Could not obtain Config VMO Handle");
  zx::vmo config_vmo(config_vmo_handle);

  // Get the size of the VMO
  uint64_t content_size_prop = 0;
  zx_status_t status = config_vmo.get_prop_content_size(&content_size_prop);
  ZX_ASSERT_MSG(status == ZX_OK, "Could not get content size of config VMO");
  size_t vmo_content_size = static_cast<size_t>(content_size_prop);

  // Checksum length must be correct
  uint16_t checksum_length = 0;
  status = config_vmo.read(&checksum_length, 0, 2);
  ZX_ASSERT_MSG(status == ZX_OK, "Could not read checksum length from config VMO");

  // Verify Checksum
  std::vector<uint8_t> checksum(checksum_length);
  status = config_vmo.read(checksum.data(), 2, checksum_length);
  ZX_ASSERT_MSG(status == ZX_OK, "Could not read checksum from config VMO");
  std::vector<uint8_t> expected_checksum {
    0xb5,
    0xf9,
    0x33,
    0xe8,
    0x94,
    0x56,
    0x3a,
    0xf9,
    0x61,
    0x39,
    0xe5,
    0x05,
    0x79,
    0x4b,
    0x88,
    0xa5,
    0x3e,
    0xd4,
    0xd1,
    0x5c,
    0x32,
    0xe2,
    0xb4,
    0x49,
    0x9e,
    0x42,
    0xeb,
    0xa3,
    0x32,
    0xb1,
    0xf5,
    0xbb
  };
  ZX_ASSERT_MSG(checksum == expected_checksum, "Invalid checksum for config VMO");

  // Read the FIDL struct into memory
  // Skip the checksum length + checksum + FIDL persistent header
  // Align the struct pointer to 8 bytes (as required by FIDL)
  size_t header = 2 + checksum_length + 8;
  size_t fidl_struct_size = vmo_content_size - header;
  void* data = memalign(8, fidl_struct_size);
  status = config_vmo.read(data, header, fidl_struct_size);
  ZX_ASSERT_MSG(status == ZX_OK, "Could not read FIDL struct from config VMO");
  uint8_t* fidl_struct = static_cast<uint8_t*>(data);

  // Decode the FIDL struct
  fidl::DecodedMessage<::cf_sc_internal::wire::Config> decoded(
    fidl::internal::WireFormatVersion::kV2, fidl_struct, static_cast<uint32_t>(fidl_struct_size));
  ZX_ASSERT_MSG(decoded.ok(), "Could not decode Config FIDL structure");
  ::cf_sc_internal::wire::Config* fidl_config = decoded.PrimaryObject();

  // Convert the configuration into a new struct
  cf_sc_internal::Config c{
            .snake_case_string = fidl_config->snake_case_string
,            .lower_camel_case_string = fidl_config->lower_camel_case_string
,            .upper_camel_case_string = fidl_config->upper_camel_case_string
,            .const_case = fidl_config->const_case
,            .string_that_has02_digits = fidl_config->string_that_has02_digits
,            .mixed_lower_camel_snake_case_string = fidl_config->mixed_lower_camel_snake_case_string
,            .mixed_upper_camel_snake_case_string = fidl_config->mixed_upper_camel_snake_case_string
,            .multiple__underscores = fidl_config->multiple__underscores
,            .unsafe_ = fidl_config->unsafe_
,            .server_mode_ = fidl_config->server_mode_
};

  // Free up allocated memory
  free(data);

  return c;
}

void cf_sc_internal::Config::record_to_inspect(sys::ComponentInspector  * inspector) {
  inspect::Node inspect_config = inspector->root().CreateChild("config");
        inspect_config.CreateBool("snake_case_string", this->snake_case_string, inspector);

        inspect_config.CreateBool("lower_camel_case_string", this->lower_camel_case_string, inspector);

        inspect_config.CreateBool("upper_camel_case_string", this->upper_camel_case_string, inspector);

        inspect_config.CreateBool("const_case", this->const_case, inspector);

        inspect_config.CreateBool("string_that_has02_digits", this->string_that_has02_digits, inspector);

        inspect_config.CreateBool("mixed_lower_camel_snake_case_string", this->mixed_lower_camel_snake_case_string, inspector);

        inspect_config.CreateBool("mixed_upper_camel_snake_case_string", this->mixed_upper_camel_snake_case_string, inspector);

        inspect_config.CreateBool("multiple__underscores", this->multiple__underscores, inspector);

        inspect_config.CreateBool("unsafe_", this->unsafe_, inspector);

        inspect_config.CreateBool("server_mode_", this->server_mode_, inspector);

  inspector->emplace(std::move(inspect_config));
}
"#;
        assert_eq!(actual_cpp_elf_src.h_source, expected_cpp_elf_src_h);
        assert_eq!(actual_cpp_elf_src.cc_source, expected_cpp_elf_src_cc);
    }
}
