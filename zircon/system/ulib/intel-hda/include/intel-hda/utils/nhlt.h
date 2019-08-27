// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTEL_HDA_UTILS_NHLT_H_
#define INTEL_HDA_UTILS_NHLT_H_

// Non-HD Audio Link Table (NHLT) definitions taken from
//
// Intel® Smart Sound Technology NHLT Specification
// Architecture Guide/Overview
// Revision 1.0, 2018-06-06
// Intel Unique ID 595976

#include <zircon/compiler.h>

#include <cstdint>

namespace audio::intel_hda {

constexpr const char* ACPI_NHLT_SIGNATURE = "NHLT";

#define ACPI_NAME_SIZE 4
#define ACPI_OEM_ID_SIZE 6
#define ACPI_OEM_TABLE_ID_SIZE 8

// Including ACPI table header definitions here to avoid including
// ACPICA header file

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
  uint32_t capabilities_size;  // In bytes, does not include size of this field.
  // followed by |capabilities_size| bytes.
} __PACKED;

struct nhlt_guid_t {
  uint8_t guid[16];
} __PACKED;

struct format_config_t {
  uint16_t format_tag;             // Tag describing this field. Always 0xFFFE.
  uint16_t n_channels;             // Number of channels.
  uint32_t n_samples_per_sec;      // Samples per second.
  uint32_t n_avg_bytes_per_sec;    // Average data transfer rate.
  uint16_t n_block_align;          // Block alignment, in bytes.
  uint16_t bits_per_sample;        // Bits per sample; always a multiple of 8. This represents
                                   // the container size, and may be larger than the actual
                                   // sample size.
  uint16_t cb_size;                // Size of following three fields in bytes: always 22.
  uint16_t valid_bits_per_sample;  // Number of bits of precision in the audio signal.
  uint32_t channel_mask;           // Assignment of channels in the stream to speaker positions.
  nhlt_guid_t subformat;           // Subformat of data.
  // followed by specific_config_t.
} __PACKED;

struct formats_config_t {
  uint8_t format_config_count;
  // followed by |format_config_count| format_config_t structures.
} __PACKED;

enum NhltLinkType : uint8_t {
  NHLT_LINK_TYPE_HDA = 0,
  NHLT_LINK_TYPE_PDM = 2,
  NHLT_LINK_TYPE_SSP = 3,
};

enum NhltEndpointDirection : uint8_t {
  NHLT_DIRECTION_RENDER = 0,
  NHLT_DIRECTION_CAPTURE = 1,
  NHLT_DIRECTION_BIDIR = 2,
};

struct nhlt_descriptor_t {
  uint32_t length;         // Size of the endpoint descriptor, including specific_config_t
                           // and formats_config_t fields.
  NhltLinkType link_type;  // Underlying link type.
  uint8_t instance_id;     // Device instance, unique to a particular link type.
                           // In the range [0, 7].

  // Vendor / Device / Revision information for driver matching.
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t revision_id;
  uint32_t subsystem_id;

  uint8_t device_type;
  NhltEndpointDirection direction;
  uint8_t virtual_bus_id;

  // followed by specific_config_t
  // followed by formats_config_t
} __PACKED;

struct nhlt_table_t {
  acpi_table_header_t header;
  uint8_t endpoint_desc_count;
  // followed by |endpoint_desc_count| nhlt_descriptor_t structures (endpoints).
  // followed by specific_config_t (oed_config);
} __PACKED;

}  // namespace audio::intel_hda

#endif  // INTEL_HDA_UTILS_NHLT_H_
