// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_

#include <stdint.h>
#include <xefi.h>
#include <zircon/compiler.h>

#define ACPI_TABLE_SIGNATURE_SIZE 4

extern const efi_guid kAcpiTableGuid;
extern const efi_guid kAcpi20TableGuid;
extern const uint8_t kAcpiRsdpSignature[8];
extern const uint8_t kRsdtSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kXsdtSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kScprSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kMadtSignature[ACPI_TABLE_SIGNATURE_SIZE];

__BEGIN_CDECLS

#define ACPI_RSDP_V1_SIZE offsetof(acpi_rsdp_t, length)

typedef struct __attribute__((packed)) {
  uint64_t signature;
  uint8_t checksum;
  uint8_t oem_id[6];
  uint8_t revision;
  uint32_t rsdt_address;

  // Available in ACPI version 2.0.
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} acpi_rsdp_t;
_Static_assert(sizeof(acpi_rsdp_t) == 36, "RSDP is the wrong size");

typedef struct __attribute__((packed)) {
  uint8_t signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  uint8_t oem_id[6];
  uint8_t oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} acpi_sdt_hdr_t;
_Static_assert(sizeof(acpi_sdt_hdr_t) == 36, "System Description Table Header is the wrong size");

// Loads the Root System Description Pointer from UEFI.
// Returns NULL if UEFI contains no such entry in its configuration table.
acpi_rsdp_t* load_acpi_rsdp(efi_configuration_table* entries, size_t num_entries);

// Loads an ACPI table with the given signature if it exists.
acpi_sdt_hdr_t* load_table_with_signature(acpi_rsdp_t* rsdp, uint8_t* signature);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_
