// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xefi.h>
#include <zircon/boot/driver-config.h>

const efi_guid kAcpiTableGuid = ACPI_TABLE_GUID;
const efi_guid kAcpi20TableGuid = ACPI_20_TABLE_GUID;
const uint8_t kAcpiRsdpSignature[8] = "RSD PTR ";
const uint8_t kRsdtSignature[ACPI_TABLE_SIGNATURE_SIZE] = "RSDT";
const uint8_t kXsdtSignature[ACPI_TABLE_SIGNATURE_SIZE] = "XSDT";
const uint8_t kSpcrSignature[ACPI_TABLE_SIGNATURE_SIZE] = "SPCR";
const uint8_t kMadtSignature[ACPI_TABLE_SIGNATURE_SIZE] = "APIC";

// Computes the checksum of an ACPI table, which is just the sum of the bytes
// in the table. The table is valid if the checksum is zero.
uint8_t acpi_checksum(void* bytes, uint32_t length) {
  uint8_t checksum = 0;
  uint8_t* data = (uint8_t*)bytes;
  for (uint32_t i = 0; i < length; i++) {
    checksum += data[i];
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
  if (acpi_checksum(rsdp, ACPI_RSDP_V1_SIZE)) {
    printf("RSDP V1 checksum failed\n");
    return NULL;
  }
  // V2 RSDPs should additionally pass a checksum of the entire table.
  if (rsdp->revision > 0) {
    if (acpi_checksum(rsdp, rsdp->length)) {
      printf("RSDP V2 checksum failed\n");
      return NULL;
    }
  }
  return rsdp;
}

// Loads the ACPI table with the given signature.
acpi_sdt_hdr_t* load_table_with_signature(acpi_rsdp_t* rsdp, uint8_t* signature) {
  uint8_t sdt_entry_size = sizeof(uint32_t);
  acpi_sdt_hdr_t* sdt_table = NULL;

  // Find the appropriate system description table, depending on the ACPI
  // version in use.
  if (rsdp->revision > 0) {
    sdt_table = (acpi_sdt_hdr_t*)rsdp->xsdt_address;
    if (memcmp(sdt_table->signature, kXsdtSignature, sizeof(kXsdtSignature))) {
      printf("XSDT signature is incorrect\n");
      return NULL;
    }
    // XSDT uses 64-bit physical addresses.
    sdt_entry_size = sizeof(uint64_t);
  } else {
    sdt_table = (acpi_sdt_hdr_t*)((efi_physical_addr)rsdp->rsdt_address);
    if (memcmp(sdt_table->signature, kRsdtSignature, sizeof(kRsdtSignature))) {
      printf("RSDT signature is incorrect\n");
      return NULL;
    }
    // RSDT uses 32-bit physical addresses.
    sdt_entry_size = sizeof(uint32_t);
  }

  // Verify the system description table is correct.
  if (acpi_checksum(sdt_table, sdt_table->length)) {
    printf("SDT checksum is incorrect\n");
    return NULL;
  }

  // Search the entries in the system description table for the table with the
  // requested signature.
  uint32_t num_entries = (sdt_table->length - sizeof(acpi_sdt_hdr_t)) / sdt_entry_size;
  for (uint32_t i = 0; i < num_entries; i++) {
    acpi_sdt_hdr_t* entry;
    if (sdt_entry_size == 4) {
      uint32_t* entries = (uint32_t*)(sdt_table + 1);
      entry = (acpi_sdt_hdr_t*)((uint64_t)entries[i]);
    } else {
      uint64_t* entries = (uint64_t*)(sdt_table + 1);
      entry = (acpi_sdt_hdr_t*)entries[i];
    }
    if (!memcmp(entry->signature, signature, ACPI_TABLE_SIGNATURE_SIZE)) {
      if (acpi_checksum(entry, entry->length)) {
        printf("table checksum is incorrect\n");
        return NULL;
      }
      return entry;
    }
  }
  return NULL;
}

uint32_t spcr_type_to_kdrv(acpi_spcr_t* spcr) {
  if (spcr == 0) {
    return 0;
  }
  // The SPCR table does not contain the granular subtype of the register
  // interface we need in revision 1, so return early in this case.
  if (spcr->hdr.revision < 2) {
    return 0;
  }
  // The SPCR types are documented in Table 3 on:
  // https://docs.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table
  // We currently only rely on PL011 devices to be initialized here.
  switch (spcr->interface_type) {
    case 0x0003:
      return KDRV_PL011_UART;
    default:
      printf("unsupported serial interface type 0x%x", spcr->interface_type);
      return 0;
  }
}

void uart_driver_from_spcr(acpi_spcr_t* spcr, dcfg_simple_t* uart_driver) {
  memset(uart_driver, 0x0, sizeof(dcfg_simple_t));
  uint32_t interrupt = 0;
  if (0x1 & spcr->interrupt_type) {
    // IRQ is only valid if the lowest order bit of interrupt type is set.
    interrupt = spcr->irq;
  } else {
    // Any other bit set to 1 in the interrupt type indicates that we should
    // use the Global System Interrupt (GSIV).
    interrupt = spcr->gsiv;
  }
  uart_driver->mmio_phys = spcr->base_address.address;
  uart_driver->irq = interrupt;
}
