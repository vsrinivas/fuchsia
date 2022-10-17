// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nhlt.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <optional>
#include <type_traits>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <intel-hda/utils/nhlt.h>

#include "binary_decoder.h"
#include "debug-logging.h"

namespace audio::intel_hda {
// Read a "specific config" NHLT section.
//
// This consists of a uint32_t |size| field, followed by |size| bytes of data.
zx::result<fbl::Vector<uint8_t>> ReadSpecificConfig(BinaryDecoder* decoder) {
  // Read the the size of the SpecificConfig structure.
  //
  // The length field indicates the number of capabilities (== the number
  // of bytes) after the field.
  auto length_status = decoder->Read<uint32_t>();
  if (!length_status.is_ok()) {
    return zx::error(length_status.status_value());
  }
  uint32_t length = std::move(length_status.value());
  // Read payload.
  zx::result<cpp20::span<const uint8_t>> payload_status = decoder->Read(length);
  if (!payload_status.is_ok()) {
    return zx::error(payload_status.status_value());
  }

  // Copy bytes into vector.
  const cpp20::span<const uint8_t>& payload = std::move(payload_status.value());

  fbl::Vector<uint8_t> result;
  fbl::AllocChecker ac;
  result.reserve(length, &ac);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  for (size_t i = 0; i < length; i++) {
    result.push_back(payload[i]);
  }
  return zx::ok(std::move(result));
}

// Parse a NHLT descriptor.
//
// This consists of:
//
//   * A header of type nhlt_descriptor.
//   * A specific config block.
//   * A byte specifying the number of formats.
//   * N format blocks.
zx::result<EndPointConfig> ParseDescriptor(const nhlt_descriptor_t& header,
                                           cpp20::span<const uint8_t> additional_bytes) {
  EndPointConfig config;
  config.header = header;
  config.bus_id = header.virtual_bus_id;
  config.direction = header.direction;

  // Read off capabilities.
  BinaryDecoder decoder(additional_bytes);
  auto result = ReadSpecificConfig(&decoder);
  if (!result.is_ok()) {
    return zx::error(result.status_value());
  }
  config.specific_config = std::move(result.value());
  // Read number of formats.
  auto format_count = decoder.Read<formats_config_t>();
  if (!format_count.is_ok()) {
    return zx::error(format_count.status_value());
  }

  // Parse formats.
  for (size_t i = 0; i < format_count.value().format_config_count; i++) {
    EndPointConfig::Format format;

    // Read the format header.
    auto format_status = decoder.Read<format_config_t>();
    if (!format_status.is_ok()) {
      return zx::error(format_status.status_value());
    }
    format.config = std::move(format_status.value());

    // Read any capabilities associated with this format.
    auto result = ReadSpecificConfig(&decoder);
    if (!result.is_ok()) {
      return zx::error(result.status_value());
    }
    format.capabilities = std::move(result.value());
    // Save the format.
    fbl::AllocChecker ac;
    config.formats.push_back(std::move(format), &ac);
    if (!ac.check()) {
      return zx::error(ZX_ERR_NO_MEMORY);
    }
  }

  return zx::ok(std::move(config));
}

zx::result<std::unique_ptr<Nhlt>> Nhlt::FromBuffer(cpp20::span<const uint8_t> buffer) {
  // Create output object.
  fbl::AllocChecker ac;
  auto result = fbl::make_unique_checked<Nhlt>(&ac);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  // Read NHLT header.
  BinaryDecoder decoder(buffer);
  auto nhlt_blob = decoder.Read<nhlt_table_t>();
  if (!nhlt_blob.is_ok()) {
    GLOBAL_LOG(DEBUG, "Could not parse ACPI NHLT header %u", nhlt_blob.status_value());
    return zx::error(nhlt_blob.status_value());
  }
  auto nhlt = std::move(nhlt_blob.value());
  static_assert(sizeof(nhlt.header.signature) >= ACPI_NAME_SIZE);
  result->oem_id_ = std::string(nhlt.header.oem_id, strnlen(nhlt.header.oem_id, ACPI_OEM_ID_SIZE));
  result->oem_table_id_ = std::string(nhlt.header.oem_table_id,
                                      strnlen(nhlt.header.oem_table_id, ACPI_OEM_TABLE_ID_SIZE));

  static_assert(std::char_traits<char>::length(ACPI_NHLT_SIGNATURE) >= ACPI_NAME_SIZE);
  if (memcmp(nhlt.header.signature, ACPI_NHLT_SIGNATURE, ACPI_NAME_SIZE) != 0) {
    GLOBAL_LOG(ERROR, "Invalid NHLT signature");
    return zx::error(ZX_ERR_INTERNAL);
  }

  // Extract the PCM formats and I2S config blob.
  for (size_t i = 0; i < nhlt.endpoint_desc_count; i++) {
    // Read descriptor.
    auto desc = decoder.VariableLengthRead<nhlt_descriptor_t>(&nhlt_descriptor_t::length);
    if (!desc.is_ok()) {
      GLOBAL_LOG(DEBUG, "Error reading NHLT descriptor header at index %lu", i);
      return zx::error(desc.status_value());
    }
    auto [desc_header, desc_additional_bytes] = std::move(desc.value());

    // Parse the descriptor.
    zx::result<EndPointConfig> config = ParseDescriptor(desc_header, desc_additional_bytes);
    if (!config.is_ok()) {
      GLOBAL_LOG(DEBUG, "Error reading NHLT descriptor body at index %lu", i);
      return zx::error(config.status_value());
    }

    // If the returned descriptor is nullopt, we don't support it. Just ignore it.
    if (config.value().header.link_type != NHLT_LINK_TYPE_SSP &&
        config.value().header.link_type != NHLT_LINK_TYPE_PDM) {
      GLOBAL_LOG(DEBUG, "Ignoring non-SSP, non-PDM NHLT descriptor at index %lu.", i);
      continue;
    }
    fbl::AllocChecker ac;
    result->configs_.push_back(std::move(config.value()), &ac);
    if (!ac.check()) {
      return zx::error(ZX_ERR_NO_MEMORY);
    }
  }

  return zx::ok(std::move(result));
}

void Nhlt::Dump() const {
  GLOBAL_LOG(INFO, "Got %lu NHLT endpoints:", configs_.size());
  size_t n = 0;
  for (const EndPointConfig& endpoint : configs_) {
    GLOBAL_LOG(INFO, "  Endpoint %lu:", n++);
    GLOBAL_LOG(INFO, "    link_type: %u", endpoint.header.link_type);
    GLOBAL_LOG(INFO, "    instance_id: %u", endpoint.header.instance_id);
    GLOBAL_LOG(INFO, "    vendor_id: 0x%x", endpoint.header.vendor_id);
    GLOBAL_LOG(INFO, "    device_id: 0x%x", endpoint.header.device_id);
    GLOBAL_LOG(INFO, "    revision_id: %u", endpoint.header.revision_id);
    GLOBAL_LOG(INFO, "    subsystem_id: %u", endpoint.header.subsystem_id);
    GLOBAL_LOG(INFO, "    device_type: %u", endpoint.header.device_type);
    GLOBAL_LOG(INFO, "    direction: %u", endpoint.header.direction);
    GLOBAL_LOG(INFO, "    virtual_bus_id: %u", endpoint.header.virtual_bus_id);
    GLOBAL_LOG(INFO, "    specific_config: %lu byte(s):", endpoint.specific_config.size());
    for (const auto& format : endpoint.formats) {
      GLOBAL_LOG(INFO, "    * Format:");
      GLOBAL_LOG(INFO,
                 "      tag=%u, n_channels=%d, n_samples_per_sec=%d, "
                 "n_avg_bytes_per_sec=%d",
                 format.config.format_tag, format.config.n_channels,
                 format.config.n_samples_per_sec, format.config.n_avg_bytes_per_sec);
      GLOBAL_LOG(INFO,
                 "      n_block_align=%d, bits_per_sample=%d, cb_size=%d, "
                 "valid_bits_per_sample=%d",
                 format.config.n_block_align, format.config.bits_per_sample, format.config.cb_size,
                 format.config.valid_bits_per_sample);
      GLOBAL_LOG(INFO, "      channel_mask=%d", format.config.channel_mask);
      GLOBAL_LOG(INFO, "      capabilities: %lu byte(s)", format.capabilities.size());
    }
  }
}

void Nhlt::DumpNhlt(const uint8_t* data, size_t length) {
  auto nhlt = Nhlt::FromBuffer(cpp20::span<const uint8_t>(data, length));
  if (!nhlt.is_ok()) {
    GLOBAL_LOG(ERROR, "Failed to parse NHLT: %u", nhlt.status_value());
    return;
  }
  nhlt.value()->Dump();
}

}  // namespace audio::intel_hda
