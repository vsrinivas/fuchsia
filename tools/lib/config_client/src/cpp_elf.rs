// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{normalize_field_key, SourceGenError};
use cm_rust::{ConfigChecksum, ConfigDecl, ConfigNestedValueType, ConfigValueType};

pub struct CppSource {
    pub cc_source: String,
    pub h_source: String,
}

pub fn create_cpp_elf_wrapper(
    config_decl: &ConfigDecl,
    cpp_namespace: String,
    fidl_library_name: String,
) -> Result<CppSource, SourceGenError> {
    let cpp_namespace = cpp_namespace.replace('.', "_").replace('-', "_").to_ascii_lowercase();
    let header_guard = fidl_library_name.replace('.', "_").to_ascii_uppercase();
    let fidl_cpp_namespace = fidl_library_name.replace('.', "_").to_ascii_lowercase();
    let ConfigChecksum::Sha256(expected_checksum) = &config_decl.checksum;
    let expected_checksum: Vec<String> =
        expected_checksum.into_iter().map(|b| format!("{:#04x}", b)).collect();
    let expected_checksum = expected_checksum.join(", ");

    let mut declarations = vec![];
    let mut conversions = vec![];

    for field in &config_decl.fields {
        let (declaration, conversion) =
            get_cpp_field_declaration_and_conversion(&field.key, &field.type_);
        declarations.push(declaration);
        conversions.push(conversion);
    }

    let declarations = declarations.join("\n");
    let conversions = conversions.join(",\n");

    // TODO(http://fxbug.dev/93026): Use a templating library instead of the format macro.
    let h_source = format!(
        r#"
#ifndef __{header_guard}_H__
#define __{header_guard}_H__

#include <string>
#include <vector>

namespace {cpp_namespace} {{
struct Config {{
{declarations}

static Config from_args() noexcept;
}};
}}

#endif
"#
    );

    let cc_source = format!(
        r#"
#include <fidl/{fidl_library_name}/cpp/wire.h>
#include <fidl/{fidl_library_name}/cpp/wire_types.h>
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
std::vector<T> from_vector_view(fidl::VectorView<T> v) {{
    size_t count = v.count();
    std::vector<T> data(count);
    for (size_t i = 0; i < count; i++) {{
        data[i] = v[i];
    }}
    return data;
}}

std::vector<std::string> from_vector_string_view(fidl::VectorView<fidl::StringView> v) {{
    size_t count = v.count();
    std::vector<std::string> data(count);
    for (size_t i = 0; i < count; i++) {{
        data[i] = std::string(v[i].get());
    }}
    return data;
}}

{cpp_namespace}::Config {cpp_namespace}::Config::from_args() noexcept {{
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
    std::vector<uint8_t> expected_checksum {{
        {expected_checksum}
    }};
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
    fidl::DecodedMessage<::{fidl_cpp_namespace}::wire::Config> decoded(
        fidl::internal::WireFormatVersion::kV2, fidl_struct, static_cast<uint32_t>(fidl_struct_size));
    ZX_ASSERT_MSG(decoded.ok(), "Could not decode Config FIDL structure");
    ::{fidl_cpp_namespace}::wire::Config* fidl_config = decoded.PrimaryObject();

    // Convert the configuration into a new struct
    {cpp_namespace}::Config c{{
{conversions}
    }};

    // Free up allocated memory
    free(data);

    return c;
}}
"#
    );
    return Ok(CppSource { cc_source, h_source });
}

fn get_cpp_field_declaration_and_conversion(
    key: &str,
    type_: &ConfigValueType,
) -> (String, String) {
    let identifier = normalize_field_key(key);
    let cpp_type = match type_ {
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
    };
    let decl = format!("{} {};", cpp_type, identifier);
    let conversion = match type_ {
        ConfigValueType::Vector { nested_type: ConfigNestedValueType::String { .. }, .. } => {
            format!(".{} = from_vector_string_view(fidl_config->{})", identifier, identifier)
        }
        ConfigValueType::Vector { .. } => {
            format!(".{} = from_vector_view(fidl_config->{})", identifier, identifier)
        }
        ConfigValueType::String { .. } => {
            format!(".{} = std::string(fidl_config->{}.get())", identifier, identifier)
        }
        _ => {
            format!(".{} = fidl_config->{}", identifier, identifier)
        }
    };
    (decl, conversion)
}
