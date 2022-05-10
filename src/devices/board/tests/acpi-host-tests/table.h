// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <zircon/compiler.h>

#include <vector>

#ifndef SRC_DEVICES_BOARD_TESTS_ACPI_HOST_TESTS_ACPI_TABLES_H
#define SRC_DEVICES_BOARD_TESTS_ACPI_HOST_TESTS_ACPI_TABLES_H

namespace acpi {

#define ZIRCON_OEM_ID \
  { 'Z', 'I', 'R', 'C', 'O', 'N' }

// Calculate the checksum for the given table.
// This adds up all the bytes in the table,
// and returns the value that needs to be in the "checksum" field
// to make it add up to zero.
//
// To verify an existing table, simply check that this function returns zero.
uint8_t ChecksumTable(void* data, size_t length);

// ACPI v6.4, 5.2.5.3 "Root System Description Pointer Structure"
struct __PACKED AcpiRsdp {
  char signature[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
  uint8_t checksum = 0;
  char oemid[6] = ZIRCON_OEM_ID;
  uint8_t revision = 2;
  uint32_t rsdt_address = 0;
  uint32_t length = sizeof(AcpiRsdp);
  uint64_t xsdt_address = 0;
  uint8_t extended_checksum = 0;
  uint8_t reserved[3];

  // Update the checksums for this table.
  void Checksum() {
    checksum = extended_checksum = 0;
    checksum = ChecksumTable(this, 20);
    extended_checksum = ChecksumTable(this, sizeof(AcpiRsdp));
  }
};
static_assert(sizeof(AcpiRsdp) == 36);

// ACPI v6.4, 5.2.6 "System Description Table Header"
struct __PACKED AcpiDescriptionTableHeader {
  char signature[4];
  uint32_t length = 0;
  uint8_t revision = 1;
  uint8_t checksum = 0;
  char oemid[6] = ZIRCON_OEM_ID;
  // The spec says this should match the Table ID in the FADT, but nothing seems to care.
  char tableid[8];
  uint32_t oem_revision = 0;
  char creatorid[4] = {'T', 'E', 'S', 'T'};
  uint32_t creatorrevision = 1;

  AcpiDescriptionTableHeader(const char* sig, uint32_t len) : length(len) {
    memcpy(signature, sig, sizeof(signature));
  }

  bool Is(const char* signature) {
    if (strlen(signature) != sizeof(this->signature)) {
      return false;
    }
    return strncmp(this->signature, signature, sizeof(this->signature)) == 0;
  }
};
static_assert(sizeof(AcpiDescriptionTableHeader) == 36);

// ACPI v6.4, 5.2.8 "Extended System Description Table"
struct __PACKED AcpiXsdt : public AcpiDescriptionTableHeader {
  // Encode an XSDT with the given entries into a byte array.
  std::vector<uint8_t> EncodeXsdt(std::vector<uint64_t> entries);
  AcpiXsdt() : AcpiDescriptionTableHeader("XSDT", 0) {}
};
static_assert(sizeof(AcpiXsdt) == 36);

}  // namespace acpi

#endif
