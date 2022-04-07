// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <xefi.h>

#include <vector>

#include <gtest/gtest.h>

namespace {

// Computes the value of the checksum byte.
uint8_t compute_checksum_byte(uint8_t *bytes, uint32_t length) {
  uint8_t accum = 0;
  for (uint32_t i = 0; i < length; i++) {
    accum += bytes[i];
  }
  return (uint8_t)(256 - accum);
}

// EfiConfigTable assembles a valid efi_configuration table that contains
// an RSDP entry.
class EfiConfigTable {
 public:
  acpi_rsdp_t rsdp_;
  EfiConfigTable(uint8_t revision, uint8_t rsdp_position);

  void CorruptRsdpGuid();
  void CorruptRsdpV1Checksum();
  void CorruptRsdpV2Checksum();
  void CorruptRsdpSignature();

  efi_configuration_table *RawTable();

 private:
  uint8_t rsdp_position_;
  std::vector<efi_configuration_table> table_;
};

EfiConfigTable::EfiConfigTable(uint8_t revision, uint8_t position) {
  // Initialize the RSDP structure.
  rsdp_position_ = position;
  rsdp_ = acpi_rsdp_t{
      .revision = revision,
  };
  memcpy(&rsdp_.signature, &kAcpiRsdpSignature, 8);
  rsdp_.checksum = compute_checksum_byte((uint8_t *)&rsdp_, ACPI_RSDP_V1_SIZE);
  if (revision >= 2) {
    rsdp_.length = sizeof(acpi_rsdp_t);
    rsdp_.extended_checksum = compute_checksum_byte((uint8_t *)&rsdp_, sizeof(acpi_rsdp_t));
  }
  efi_guid guid = ACPI_TABLE_GUID;
  if (revision >= 2) {
    guid = ACPI_20_TABLE_GUID;
  }

  // Construct the EFI configuration table.
  table_.resize(rsdp_position_);
  table_.push_back(efi_configuration_table{
      .VendorGuid = guid,
      .VendorTable = &rsdp_,
  });
}

void EfiConfigTable::CorruptRsdpGuid() { table_[rsdp_position_].VendorGuid.data1 ^= 0x1; }

void EfiConfigTable::CorruptRsdpSignature() {
  rsdp_.signature ^= 0x1;
  // The checksums should still be correct.
  rsdp_.checksum = compute_checksum_byte((uint8_t *)&rsdp_, ACPI_RSDP_V1_SIZE);
  if (rsdp_.revision >= 2) {
    rsdp_.extended_checksum = compute_checksum_byte((uint8_t *)&rsdp_, sizeof(acpi_rsdp_t));
  }
}

void EfiConfigTable::CorruptRsdpV1Checksum() {
  rsdp_.checksum ^= 0x1;
  // The v2 checksum, if present, should still be correct.
  if (rsdp_.revision >= 2) {
    rsdp_.extended_checksum = compute_checksum_byte((uint8_t *)&rsdp_, sizeof(acpi_rsdp_t));
  }
}

void EfiConfigTable::CorruptRsdpV2Checksum() { rsdp_.extended_checksum ^= 0x1; }

efi_configuration_table *EfiConfigTable::RawTable() { return &table_[0]; }

TEST(Acpi, RsdpMissing) {
  auto efi_config_table = EfiConfigTable(1, 0);
  efi_config_table.CorruptRsdpGuid();
  EXPECT_EQ(load_acpi_rsdp(efi_config_table.RawTable(), 1), nullptr);
}

TEST(Acpi, RsdpBadSignature) {
  auto efi_config_table = EfiConfigTable(1, 0);
  efi_config_table.CorruptRsdpSignature();
  EXPECT_EQ(load_acpi_rsdp(efi_config_table.RawTable(), 1), nullptr);
}

TEST(Acpi, RsdpBadV1Checksum) {
  auto efi_config_table = EfiConfigTable(1, 0);
  efi_config_table.CorruptRsdpV1Checksum();
  EXPECT_EQ(load_acpi_rsdp(efi_config_table.RawTable(), 1), nullptr);
}

TEST(Acpi, RsdpV1Success) {
  auto efi_config_table = EfiConfigTable(1, 0);
  EXPECT_EQ(load_acpi_rsdp(efi_config_table.RawTable(), 1), &efi_config_table.rsdp_);
}

TEST(Acpi, RsdpBadV2Checksum) {
  auto efi_config_table = EfiConfigTable(2, 0);
  efi_config_table.CorruptRsdpV2Checksum();
  EXPECT_EQ(load_acpi_rsdp(efi_config_table.RawTable(), 1), nullptr);
}

TEST(Acpi, RsdpV2Success) {
  auto efi_config_table = EfiConfigTable(2, 0);
  EXPECT_EQ(load_acpi_rsdp(efi_config_table.RawTable(), 1), &efi_config_table.rsdp_);
}

TEST(Acpi, RsdpAtEnd) {
  auto efi_config_table = EfiConfigTable(2, 5);
  EXPECT_EQ(load_acpi_rsdp(efi_config_table.RawTable(), 6), &efi_config_table.rsdp_);
}

}  // namespace
