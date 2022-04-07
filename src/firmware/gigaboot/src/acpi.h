// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_

#include <stdint.h>
#include <xefi.h>
#include <zircon/compiler.h>

extern const efi_guid kAcpiTableGuid;
extern const efi_guid kAcpi20TableGuid;
extern const uint8_t kAcpiRsdpSignature[8];

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

// Loads the Root System Description Pointer from UEFI.
// Returns NULL if UEFI contains no such entry in its configuration table.
acpi_rsdp_t* load_acpi_rsdp(efi_configuration_table* entries, size_t num_entries);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_
