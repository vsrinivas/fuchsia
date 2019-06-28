// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTEL_HDA_UTILS_NHLT_H_
#define INTEL_HDA_UTILS_NHLT_H_

/**
 * Non-HD Audio Link Table (NHLT) definitions taken from
 *
 * Intel Smart Sound Technology Audio DSP Non-HD Audio ACPI High Level Design
 * Revision 0.7
 * November 2015
 */

// Including ACPI table header definitions here to avoid including
// ACPICA header file

#if !__cplusplus
#error "C++ only header"
#else

namespace audio {
namespace intel_hda {

constexpr const char* ACPI_NHLT_SIGNATURE = "NHLT";

#define ACPI_NAME_SIZE 4
#define ACPI_OEM_ID_SIZE 6
#define ACPI_OEM_TABLE_ID_SIZE 8

struct acpi_table_header_t {
  char signature[ACPI_NAME_SIZE];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oem_id[ACPI_OEM_ID_SIZE];
  char oem_table_id[ACPI_OEM_TABLE_ID_SIZE];
  uint32_t oem_revision;
  char asl_compiler_id[ACPI_NAME_SIZE];
  uint32_t asl_compiler_revision;
} __PACKED;

struct specific_config_t {
  uint32_t capabilities_size;
  uint8_t capabilities[];
} __PACKED;

struct format_config_t {
  uint16_t format_tag;
  uint16_t n_channels;
  uint32_t n_samples_per_sec;
  uint32_t n_avg_bytes_per_sec;
  uint16_t n_block_align;
  uint16_t bits_per_sample;
  uint16_t cb_size;
  uint16_t valid_bits_per_sample;
  uint32_t channel_mask;
  uint8_t subformat_guid[16];
  specific_config_t config;
} __PACKED;

struct formats_config_t {
  uint8_t format_config_count;
  format_config_t format_configs[];
} __PACKED;

struct nhlt_descriptor_t {
  uint32_t length;
  uint8_t link_type;
  uint8_t instance_id;
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t revision_id;
  uint32_t subsystem_id;
  uint8_t device_type;
  uint8_t direction;
  uint8_t virtual_bus_id;
  specific_config_t config;
  // followed by formats_config_t format_configs
} __PACKED;

constexpr uint8_t NHLT_LINK_TYPE_HDA = 0;
constexpr uint8_t NHLT_LINK_TYPE_PDM = 2;
constexpr uint8_t NHLT_LINK_TYPE_SSP = 3;

constexpr uint8_t NHLT_DIRECTION_RENDER = 0;
constexpr uint8_t NHLT_DIRECTION_CAPTURE = 1;
constexpr uint8_t NHLT_DIRECTION_BIDIR = 2;

struct nhlt_table_t {
  acpi_table_header_t header;
  uint8_t endpoint_desc_count;
  nhlt_descriptor_t endpoints[];
  // followed by specific_config_t oed_config;
} __PACKED;

}  // namespace intel_hda
}  // namespace audio

#endif

#endif  // INTEL_HDA_UTILS_NHLT_H_
