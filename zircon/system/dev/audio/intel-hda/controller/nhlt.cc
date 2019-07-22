// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_printf.h>
#include <intel-hda/utils/nhlt.h>

#include "intel-dsp.h"

namespace audio::intel_hda {

Status IntelDsp::ParseNhlt() {
  size_t size = 0;
  zx_status_t res =
      device_get_metadata(codec_device(), *reinterpret_cast<const uint32_t*>(ACPI_NHLT_SIGNATURE),
                          nhlt_buf_, sizeof(nhlt_buf_), &size);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to fetch NHLT (res %d)\n", res);
    return Status(res);
  }

  nhlt_table_t* nhlt = reinterpret_cast<nhlt_table_t*>(nhlt_buf_);

  // Sanity check
  if (size < sizeof(*nhlt)) {
    return Status(ZX_ERR_INTERNAL, fbl::StringPrintf("NHLT too small (%zu bytes)\n", size));
  }

  static_assert(sizeof(nhlt->header.signature) >= ACPI_NAME_SIZE, "");
  static_assert(sizeof(ACPI_NHLT_SIGNATURE) >= ACPI_NAME_SIZE, "");

  if (memcmp(nhlt->header.signature, ACPI_NHLT_SIGNATURE, ACPI_NAME_SIZE) != 0) {
    return Status(ZX_ERR_INTERNAL, "Invalid NHLT signature");
  }

  uint8_t count = nhlt->endpoint_desc_count;
  if (count > I2S_CONFIG_MAX) {
    LOG(INFO,
        "Too many NHLT endpoints (max %zu, got %u), "
        "only the first %zu will be processed\n",
        I2S_CONFIG_MAX, count, I2S_CONFIG_MAX);
    count = I2S_CONFIG_MAX;
  }

  // Extract the PCM formats and I2S config blob
  size_t i = 0;
  size_t desc_offset = reinterpret_cast<uint8_t*>(nhlt->endpoints) - nhlt_buf_;
  while (count--) {
    auto desc = reinterpret_cast<nhlt_descriptor_t*>(nhlt_buf_ + desc_offset);

    // Sanity check
    if ((desc_offset + desc->length) > size) {
      return Status(ZX_ERR_INTERNAL, "NHLT endpoint descriptor out of bounds");
    }

    size_t length = static_cast<size_t>(desc->length);
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
        nhlt_buf_ + desc_offset + sizeof(*desc) + desc->config.capabilities_size);
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

    i2s_configs_[i++] = {desc->virtual_bus_id, desc->direction, formats};

    desc_offset += desc->length;
  }

  LOG(TRACE, "parse success, found %zu formats\n", i);
  return OkStatus();
}

void IntelDsp::DumpNhlt(const nhlt_table_t* table, size_t length) {
  if (length < sizeof(*table)) {
    LOG(ERROR, "NHLT too small (%zu bytes)\n", length);
    return;
  }

  if (memcmp(table->header.signature, ACPI_NHLT_SIGNATURE, ACPI_NAME_SIZE) != 0) {
    LOG(ERROR, "Invalid NHLT signature (expected '%s', got '%s')\n", ACPI_NHLT_SIGNATURE,
        table->header.signature);
    return;
  }

  uint8_t count = table->endpoint_desc_count;
  const nhlt_descriptor_t* desc = table->endpoints;
  LOG(INFO, "Got %u NHLT endpoints:\n", count);
  while (count--) {
    LOG(INFO, "Endpoint @ %p\n", desc);
    LOG(INFO, "  link_type: %u\n", desc->link_type);
    LOG(INFO, "  instance_id: %u\n", desc->instance_id);
    LOG(INFO, "  vendor_id: 0x%x\n", desc->vendor_id);
    LOG(INFO, "  device_id: 0x%x\n", desc->device_id);
    LOG(INFO, "  revision_id: %u\n", desc->revision_id);
    LOG(INFO, "  subsystem_id: %u\n", desc->subsystem_id);
    LOG(INFO, "  device_type: %u\n", desc->device_type);
    LOG(INFO, "  direction: %u\n", desc->direction);
    LOG(INFO, "  virtual_bus_id: %u\n", desc->virtual_bus_id);
    LOG(INFO, "  specific config @ %p size 0x%x\n", &desc->config, desc->config.capabilities_size);

    auto formats = reinterpret_cast<const formats_config_t*>(
        reinterpret_cast<const uint8_t*>(&desc->config) + sizeof(desc->config.capabilities_size) +
        desc->config.capabilities_size);
    LOG(INFO, "  formats_config  @ %p count %u\n", formats, formats->format_config_count);

    desc = reinterpret_cast<const nhlt_descriptor_t*>(reinterpret_cast<const uint8_t*>(desc) +
                                                      desc->length);
    if (static_cast<size_t>(reinterpret_cast<const uint8_t*>(desc) -
                            reinterpret_cast<const uint8_t*>(table)) > length) {
      LOG(ERROR, "descriptor at %p out of bounds\n", desc);
      break;
    }
  }
}

}  // namespace audio::intel_hda
