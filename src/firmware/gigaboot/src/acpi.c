// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xefi.h>

const efi_guid kAcpiTableGuid = ACPI_TABLE_GUID;
const efi_guid kAcpi20TableGuid = ACPI_20_TABLE_GUID;
const uint8_t kAcpiRsdpSignature[8] = "RSD PTR ";

// Computes the checksum of an ACPI table, which is just the sum of the bytes
// in the table. The table is valid if the checksum is zero.
uint8_t acpi_checksum(uint8_t* bytes, uint32_t length) {
  uint8_t checksum = 0;
  for (uint32_t i = 0; i < length; i++) {
    checksum += bytes[i];
  }
  return checksum;
}

acpi_rsdp_t* load_acpi_rsdp(efi_configuration_table* entries, size_t num_entries) {
  acpi_rsdp_t* rsdp = NULL;
  for (size_t i = 0; i < num_entries; i++) {
    // Check if this entry is an ACPI RSD PTR.
    if (!xefi_cmp_guid(&entries[i].VendorGuid, &kAcpiTableGuid) ||
        !xefi_cmp_guid(&entries[i].VendorGuid, &kAcpi20TableGuid)) {
      // Verify the signature of the ACPI RSD PTR.
      if (!memcmp(entries[i].VendorTable, kAcpiRsdpSignature, sizeof(kAcpiRsdpSignature))) {
        rsdp = (acpi_rsdp_t*)entries[i].VendorTable;
        break;
      }
    }
  }

  // Verify an ACPI table was found.
  if (rsdp == NULL) {
    printf("RSDP was not found\n");
    return NULL;
  }

  // Verify the checksum of this table. Both V1 and V2 RSDPs should pass the
  // V1 checksum, which only covers the first 20 bytes of the table.
  if (acpi_checksum((uint8_t*)rsdp, ACPI_RSDP_V1_SIZE)) {
    printf("RSDP V1 checksum failed\n");
    return NULL;
  }
  // V2 RSDPs should additionally pass a checksum of the entire table.
  if (rsdp->revision > 0) {
    if (acpi_checksum((uint8_t*)rsdp, rsdp->length)) {
      printf("RSDP V2 checksum failed\n");
      return NULL;
    }
  }
  return rsdp;
}
