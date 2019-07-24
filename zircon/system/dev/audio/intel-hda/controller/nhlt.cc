// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nhlt.h"

#include <zircon/errors.h>

#include <cstdint>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <intel-hda/utils/nhlt.h>
#include <intel-hda/utils/status.h>

#include "debug-logging.h"

namespace audio::intel_hda {

StatusOr<std::unique_ptr<Nhlt>> Nhlt::FromBuffer(fbl::Array<uint8_t> buffer) {
  // Create output object.
  fbl::AllocChecker ac;
  auto result = fbl::make_unique_checked<Nhlt>(&ac);
  if (!ac.check()) {
    return Status(ZX_ERR_NO_MEMORY);
  }

  const auto* nhlt = reinterpret_cast<const nhlt_table_t*>(buffer.begin());

  // Sanity check
  if (buffer.size() < sizeof(*nhlt)) {
    return Status(ZX_ERR_INTERNAL,
                  fbl::StringPrintf("NHLT too small (%zu bytes)\n", buffer.size()));
  }

  static_assert(sizeof(nhlt->header.signature) >= ACPI_NAME_SIZE);
  static_assert(std::char_traits<char>::length(ACPI_NHLT_SIGNATURE) >= ACPI_NAME_SIZE);

  if (memcmp(nhlt->header.signature, ACPI_NHLT_SIGNATURE, ACPI_NAME_SIZE) != 0) {
    return Status(ZX_ERR_INTERNAL, "Invalid NHLT signature");
  }

  // Extract the PCM formats and I2S config blob
  size_t desc_offset = reinterpret_cast<const uint8_t*>(nhlt->endpoints) - buffer.begin();
  for (size_t i = 0; i < nhlt->endpoint_desc_count; i++) {
    const auto* desc = reinterpret_cast<const nhlt_descriptor_t*>(buffer.begin() + desc_offset);

    // Sanity check
    if ((desc_offset + desc->length) > buffer.size()) {
      return Status(ZX_ERR_INTERNAL, "NHLT endpoint descriptor out of bounds");
    }

    auto length = static_cast<size_t>(desc->length);
    if (length < sizeof(*desc)) {
      return Status(ZX_ERR_INTERNAL, "Short NHLT descriptor");
    }
    length -= sizeof(*desc);

    // Only care about SSP endpoints
    if (desc->link_type != NHLT_LINK_TYPE_SSP) {
      continue;
    }

    // Make sure there is enough room for formats_configs
    if (length < desc->config.capabilities_size + sizeof(formats_config_t)) {
      return Status(ZX_ERR_INTERNAL,
                    "NHLT endpoint descriptor too short (specific_config too long)");
    }
    length -= desc->config.capabilities_size + sizeof(formats_config_t);

    // Must have at least one format
    auto formats = reinterpret_cast<const formats_config_t*>(
        buffer.begin() + desc_offset + sizeof(*desc) + desc->config.capabilities_size);
    if (formats->format_config_count == 0) {
      continue;
    }

    // Iterate the formats and check lengths
    const format_config_t* format = formats->format_configs;
    for (uint8_t j = 0; j < formats->format_config_count; j++) {
      size_t format_length = sizeof(*format) + format->config.capabilities_size;
      if (length < format_length) {
        return Status(ZX_ERR_INTERNAL, "Invalid NHLT endpoint desciptor format too short");
      }
      length -= format_length;
      format = reinterpret_cast<const format_config_t*>(reinterpret_cast<const uint8_t*>(format) +
                                                        format_length);
    }
    if (length != 0) {
      return Status(ZX_ERR_INTERNAL, "Invalid NHLT endpoint descriptor length");
    }

    // Save the config.
    I2SConfig config = {desc->virtual_bus_id, desc->direction, formats};
    fbl::AllocChecker ac;
    result->i2s_configs_.push_back(config, &ac);
    if (!ac.check()) {
      return Status(ZX_ERR_NO_MEMORY);
    }

    desc_offset += desc->length;
  }

  result->buffer_ = std::move(buffer);
  return result;
}

void Nhlt::DumpNhlt(const uint8_t* data, size_t length) {
  const auto* table = reinterpret_cast<const nhlt_table_t*>(data);

  if (length < sizeof(*table)) {
    GLOBAL_LOG(ERROR, "NHLT too small (%zu bytes)\n", length);
    return;
  }

  if (memcmp(table->header.signature, ACPI_NHLT_SIGNATURE, ACPI_NAME_SIZE) != 0) {
    GLOBAL_LOG(ERROR, "Invalid NHLT signature (expected '%s', got '%s')\n", ACPI_NHLT_SIGNATURE,
               table->header.signature);
    return;
  }

  uint8_t count = table->endpoint_desc_count;
  const nhlt_descriptor_t* desc = table->endpoints;
  GLOBAL_LOG(INFO, "Got %u NHLT endpoints:\n", count);
  while (count--) {
    GLOBAL_LOG(INFO, "Endpoint @ %p\n", desc);
    GLOBAL_LOG(INFO, "  link_type: %u\n", desc->link_type);
    GLOBAL_LOG(INFO, "  instance_id: %u\n", desc->instance_id);
    GLOBAL_LOG(INFO, "  vendor_id: 0x%x\n", desc->vendor_id);
    GLOBAL_LOG(INFO, "  device_id: 0x%x\n", desc->device_id);
    GLOBAL_LOG(INFO, "  revision_id: %u\n", desc->revision_id);
    GLOBAL_LOG(INFO, "  subsystem_id: %u\n", desc->subsystem_id);
    GLOBAL_LOG(INFO, "  device_type: %u\n", desc->device_type);
    GLOBAL_LOG(INFO, "  direction: %u\n", desc->direction);
    GLOBAL_LOG(INFO, "  virtual_bus_id: %u\n", desc->virtual_bus_id);
    GLOBAL_LOG(INFO, "  specific config @ %p size 0x%x\n", &desc->config,
               desc->config.capabilities_size);

    auto formats = reinterpret_cast<const formats_config_t*>(
        reinterpret_cast<const uint8_t*>(&desc->config) + sizeof(desc->config.capabilities_size) +
        desc->config.capabilities_size);
    GLOBAL_LOG(INFO, "  formats_config  @ %p count %u\n", formats, formats->format_config_count);

    desc = reinterpret_cast<const nhlt_descriptor_t*>(reinterpret_cast<const uint8_t*>(desc) +
                                                      desc->length);
    if (static_cast<size_t>(reinterpret_cast<const uint8_t*>(desc) -
                            reinterpret_cast<const uint8_t*>(table)) > length) {
      GLOBAL_LOG(ERROR, "descriptor at %p out of bounds\n", desc);
      break;
    }
  }
}

void Nhlt::Dump() const { DumpNhlt(buffer_.begin(), buffer_.size()); }

}  // namespace audio::intel_hda
