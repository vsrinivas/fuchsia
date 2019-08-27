// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nhlt.h"

#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <optional>
#include <type_traits>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/span.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <intel-hda/utils/nhlt.h>
#include <intel-hda/utils/status.h>

#include "binary_decoder.h"
#include "debug-logging.h"

namespace audio::intel_hda {
// Read a "specific config" NHLT section.
//
// This consists of a uint32_t |size| field, followed by |size| bytes of data.
StatusOr<fbl::Vector<uint8_t>> ReadSpecificConfig(BinaryDecoder* decoder) {
  // Read the the size of the SpecificConfig structure.
  //
  // The length field indicates the number of capabilities (== the number
  // of bytes) after the field.
  auto maybe_length = decoder->Read<uint32_t>();
  if (!maybe_length.ok()) {
    return maybe_length.status();
  }
  uint32_t length = maybe_length.ValueOrDie();

  // Read payload.
  StatusOr<fbl::Span<const uint8_t>> maybe_payload = decoder->Read(length);
  if (!maybe_payload.ok()) {
    return maybe_payload.status();
  }

  // Copy bytes into vector.
  const fbl::Span<const uint8_t>& payload = maybe_payload.ValueOrDie();
  fbl::Vector<uint8_t> result;
  fbl::AllocChecker ac;
  result.reserve(length, &ac);
  if (!ac.check()) {
    return Status(ZX_ERR_NO_MEMORY);
  }
  for (size_t i = 0; i < length; i++) {
    result.push_back(payload[i]);
  }
  return result;
}

// Parse a NHLT descriptor.
//
// This consists of:
//
//   * A header of type nhlt_descriptor.
//   * A specifc config block.
//   * A byte specifying the number of formats.
//   * N format blocks.
StatusOr<I2SConfig> ParseDescriptor(const nhlt_descriptor_t& header,
                                    fbl::Span<const uint8_t> additional_bytes) {
  I2SConfig config;
  config.header = header;
  config.bus_id = header.virtual_bus_id;
  config.direction = header.direction;

  // Read off capabilities.
  BinaryDecoder decoder(additional_bytes);
  StatusOr<fbl::Vector<uint8_t>> result = ReadSpecificConfig(&decoder);
  if (!result.ok()) {
    return result.status();
  }
  config.specific_config = result.ConsumeValueOrDie();

  // Read number of formats.
  auto maybe_format_count = decoder.Read<formats_config_t>();
  if (!maybe_format_count.ok()) {
    return maybe_format_count.status();
  }

  // Parse formats.
  for (size_t i = 0; i < maybe_format_count.ValueOrDie().format_config_count; i++) {
    I2SConfig::Format format;

    // Read the format header.
    auto maybe_format = decoder.Read<format_config_t>();
    if (!maybe_format.ok()) {
      return maybe_format.status();
    }
    format.config = maybe_format.ValueOrDie();

    // Read any capabilities associated with this format.
    auto result = ReadSpecificConfig(&decoder);
    if (!result.ok()) {
      return result.status();
    }
    format.capabilities = result.ConsumeValueOrDie();

    // Save the format.
    fbl::AllocChecker ac;
    config.formats.push_back(std::move(format), &ac);
    if (!ac.check()) {
      return Status(ZX_ERR_NO_MEMORY);
    }
  }

  return config;
}

StatusOr<std::unique_ptr<Nhlt>> Nhlt::FromBuffer(fbl::Span<const uint8_t> buffer) {
  // Create output object.
  fbl::AllocChecker ac;
  auto result = fbl::make_unique_checked<Nhlt>(&ac);
  if (!ac.check()) {
    return Status(ZX_ERR_NO_MEMORY);
  }

  // Read NHLT header.
  BinaryDecoder decoder(buffer);
  auto maybe_nhlt = decoder.Read<nhlt_table_t>();
  if (!maybe_nhlt.ok()) {
    return PrependMessage("Could not parse ACPI NHLT header", maybe_nhlt.status());
  }
  auto nhlt = maybe_nhlt.ValueOrDie();
  static_assert(sizeof(nhlt.header.signature) >= ACPI_NAME_SIZE);
  static_assert(std::char_traits<char>::length(ACPI_NHLT_SIGNATURE) >= ACPI_NAME_SIZE);
  if (memcmp(nhlt.header.signature, ACPI_NHLT_SIGNATURE, ACPI_NAME_SIZE) != 0) {
    return Status(ZX_ERR_INTERNAL, "Invalid NHLT signature");
  }

  // Extract the PCM formats and I2S config blob.
  for (size_t i = 0; i < nhlt.endpoint_desc_count; i++) {
    // Read descriptor.
    auto maybe_desc = decoder.VariableLengthRead<nhlt_descriptor_t>(&nhlt_descriptor_t::length);
    if (!maybe_desc.ok()) {
      return PrependMessage(
          fbl::StringPrintf("Error reading NHLT descriptor header at index %lu", i),
          maybe_desc.status());
    }
    auto [desc_header, desc_additional_bytes] = maybe_desc.ValueOrDie();

    // Parse the descriptor.
    StatusOr<I2SConfig> maybe_config = ParseDescriptor(desc_header, desc_additional_bytes);
    if (!maybe_config.ok()) {
      return PrependMessage(fbl::StringPrintf("Error reading NHLT descriptor body at index %lu", i),
                            maybe_config.status());
    }

    // If the returned descriptor is nullopt, we don't support it. Just ignore it.
    if (maybe_config.ValueOrDie().header.link_type != NHLT_LINK_TYPE_SSP) {
      GLOBAL_LOG(TRACE, "Ignoring non-SSP NHLT descriptor at index %lu.", i);
      continue;
    }
    fbl::AllocChecker ac;
    result->i2s_configs_.push_back(maybe_config.ConsumeValueOrDie(), &ac);
    if (!ac.check()) {
      return Status(ZX_ERR_NO_MEMORY);
    }
  }

  return result;
}

void Nhlt::Dump() const {
  GLOBAL_LOG(INFO, "Got %lu NHLT endpoints:\n", i2s_configs_.size());
  size_t n = 0;
  for (const I2SConfig& endpoint : i2s_configs_) {
    GLOBAL_LOG(INFO, "  Endpoint %lu:\n", n++);
    GLOBAL_LOG(INFO, "    link_type: %u\n", endpoint.header.link_type);
    GLOBAL_LOG(INFO, "    instance_id: %u\n", endpoint.header.instance_id);
    GLOBAL_LOG(INFO, "    vendor_id: 0x%x\n", endpoint.header.vendor_id);
    GLOBAL_LOG(INFO, "    device_id: 0x%x\n", endpoint.header.device_id);
    GLOBAL_LOG(INFO, "    revision_id: %u\n", endpoint.header.revision_id);
    GLOBAL_LOG(INFO, "    subsystem_id: %u\n", endpoint.header.subsystem_id);
    GLOBAL_LOG(INFO, "    device_type: %u\n", endpoint.header.device_type);
    GLOBAL_LOG(INFO, "    direction: %u\n", endpoint.header.direction);
    GLOBAL_LOG(INFO, "    virtual_bus_id: %u\n", endpoint.header.virtual_bus_id);
    GLOBAL_LOG(INFO, "    specific_config: %lu byte(s):\n", endpoint.specific_config.size());
    for (const auto& format : endpoint.formats) {
      GLOBAL_LOG(INFO, "    * Format:\n");
      GLOBAL_LOG(INFO,
                 "      tag=%u, n_channels=%d, n_samples_per_sec=%d, "
                 "n_avg_bytes_per_sec=%d\n",
                 format.config.format_tag, format.config.n_channels,
                 format.config.n_samples_per_sec, format.config.n_avg_bytes_per_sec);
      GLOBAL_LOG(INFO,
                 "      n_block_align=%d, bits_per_sample=%d, cb_size=%d, "
                 "valid_bits_per_sample=%d\n",
                 format.config.n_block_align, format.config.bits_per_sample, format.config.cb_size,
                 format.config.valid_bits_per_sample);
      GLOBAL_LOG(INFO, "      channel_mask=%d\n", format.config.channel_mask);
      GLOBAL_LOG(INFO, "      capabilities: %lu byte(s)\n", format.capabilities.size());
    }
  }
}

void Nhlt::DumpNhlt(const uint8_t* data, size_t length) {
  auto maybe_nhlt = Nhlt::FromBuffer(fbl::Span<const uint8_t>(data, length));
  if (!maybe_nhlt.ok()) {
    GLOBAL_LOG(ERROR, "Failed to parse NHLT: %s\n", maybe_nhlt.status().ToString().c_str());
    return;
  }
  maybe_nhlt.ValueOrDie()->Dump();
}

}  // namespace audio::intel_hda
