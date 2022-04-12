// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <xefi.h>
#include <zircon/boot/driver-config.h>

#include <vector>

#include <gtest/gtest.h>

namespace {

#define ROUNDUP(size, align) (((size) + ((align)-1)) & ~((align)-1))

// Update the value of the given checksum.
void update_checksum(void *data, size_t size, uint8_t &checksum) {
  checksum = 0;
  uint8_t accum = 0;
  uint8_t *buf = (uint8_t *)data;
  for (size_t i = 0; i < size; i++) {
    accum += buf[i];
  }
  checksum = (uint8_t)(0x100 - accum);
}

// EfiConfigTable assembles a valid efi_configuration table that contains
// an RSDP entry.
class EfiConfigTable {
 public:
  // A fake XSDT table structure that allows for 4 table entries.
  struct __attribute__((packed)) fake_acpi_xsdt {
    // We set up this padding so that the start of the entries array is 64-bit
    // aligned, while also maintaining that the header and entries array are
    // adjacent as ACPI expects.
    uint8_t padding[4];
    acpi_sdt_hdr_t hdr;
    // We only test the extended system description table as the root system
    // description table uses 32-bit physical addresses, which we can't test
    // in our 64-bit host toolchain.
    uint64_t entries[4];
  };
  fake_acpi_xsdt xsdt_;
  acpi_rsdp_t rsdp_;
  EfiConfigTable(uint8_t revision, uint8_t rsdp_position);
  ~EfiConfigTable();

  void CorruptRsdpGuid();
  void CorruptRsdpV1Checksum();
  void CorruptRsdpV2Checksum();
  void CorruptRsdpSignature();
  void CorruptXsdtSignature();
  void CorruptXsdtChecksum();
  acpi_sdt_hdr_t *AddAcpiTable(const uint8_t *signature);

  efi_configuration_table *RawTable();

 private:
  uint8_t rsdp_position_;
  std::vector<efi_configuration_table> table_;
};

EfiConfigTable::EfiConfigTable(uint8_t revision, uint8_t position) {
  // Initialize the XSDT structure.
  memset(&xsdt_, 0, sizeof(xsdt_));
  memcpy(&xsdt_.hdr.signature, &kXsdtSignature, sizeof(kXsdtSignature));
  xsdt_.hdr.revision = 1;
  memset(xsdt_.entries, 0, sizeof(xsdt_.entries));
  xsdt_.hdr.length = sizeof(acpi_sdt_hdr_t);
  update_checksum(&xsdt_.hdr, xsdt_.hdr.length, xsdt_.hdr.checksum);

  // Initialize the RSDP structure.
  rsdp_position_ = position;
  rsdp_ = acpi_rsdp_t{
      .revision = revision,
  };
  memcpy(&rsdp_.signature, &kAcpiRsdpSignature, sizeof(kAcpiRsdpSignature));
  update_checksum(&rsdp_, ACPI_RSDP_V1_SIZE, rsdp_.checksum);
  if (revision >= 2) {
    rsdp_.xsdt_address = (uint64_t)&xsdt_.hdr;
    rsdp_.length = sizeof(acpi_rsdp_t);
    update_checksum(&rsdp_, rsdp_.length, rsdp_.extended_checksum);
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

EfiConfigTable::~EfiConfigTable() {
  // Free up all of the memory used by the ACPI tables.
  auto num_entries = (xsdt_.hdr.length - sizeof(acpi_sdt_hdr_t)) / sizeof(uint64_t);
  for (size_t i = 0; i < num_entries; i++) {
    delete (uint8_t *)xsdt_.entries[i];
  }
}

void EfiConfigTable::CorruptRsdpGuid() { table_[rsdp_position_].VendorGuid.data1 ^= 0x1; }

void EfiConfigTable::CorruptRsdpSignature() {
  rsdp_.signature ^= 0x1;
  // The checksums should still be correct.
  update_checksum(&rsdp_, ACPI_RSDP_V1_SIZE, rsdp_.checksum);
  if (rsdp_.revision >= 2) {
    update_checksum(&rsdp_, rsdp_.length, rsdp_.extended_checksum);
  }
}

void EfiConfigTable::CorruptRsdpV1Checksum() {
  rsdp_.checksum ^= 0x1;
  // The v2 checksum, if present, should still be correct.
  if (rsdp_.revision >= 2) {
    update_checksum(&rsdp_, rsdp_.length, rsdp_.extended_checksum);
  }
}

void EfiConfigTable::CorruptRsdpV2Checksum() { rsdp_.extended_checksum ^= 0x1; }

void EfiConfigTable::CorruptXsdtSignature() {
  xsdt_.hdr.signature[0] ^= 1;
  update_checksum(&xsdt_.hdr, xsdt_.hdr.length, xsdt_.hdr.checksum);
}

void EfiConfigTable::CorruptXsdtChecksum() { xsdt_.hdr.checksum ^= 1; }

acpi_sdt_hdr_t *EfiConfigTable::AddAcpiTable(const uint8_t *signature) {
  // Create the table header. We don't need to initialize a full table.
  auto hdr = new acpi_sdt_hdr_t{};
  memcpy(&hdr->signature, signature, ACPI_TABLE_SIGNATURE_SIZE);
  hdr->length = sizeof(acpi_sdt_hdr_t);
  update_checksum(hdr, hdr->length, hdr->checksum);

  // Add the header pointer to the XSDT.
  auto num_entries = (xsdt_.hdr.length - sizeof(acpi_sdt_hdr_t)) / sizeof(uint64_t);
  if (num_entries >= sizeof(xsdt_.entries) / sizeof(uint64_t)) {
    delete hdr;
    return nullptr;
  }
  xsdt_.entries[num_entries] = (uint64_t)hdr;
  xsdt_.hdr.length += sizeof(uint64_t);
  update_checksum(&xsdt_.hdr, xsdt_.hdr.length, xsdt_.hdr.checksum);

  return hdr;
}

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

TEST(Acpi, LoadBySignatureInvalidXsdtSignature) {
  auto efi_config_table = EfiConfigTable(2, 0);
  efi_config_table.AddAcpiTable((uint8_t *)kSpcrSignature);
  efi_config_table.CorruptXsdtSignature();
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), nullptr);
}

TEST(Acpi, LoadBySignatureInvalidXsdtChecksum) {
  auto efi_config_table = EfiConfigTable(2, 0);
  efi_config_table.AddAcpiTable((uint8_t *)kSpcrSignature);
  efi_config_table.CorruptXsdtChecksum();
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), nullptr);
}

TEST(Acpi, LoadBySignatureTableNotFound) {
  auto efi_config_table = EfiConfigTable(2, 0);
  EXPECT_NE(efi_config_table.AddAcpiTable((uint8_t *)kMadtSignature), nullptr);
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), nullptr);
}

TEST(Acpi, LoadBySignatureInvalidTableChecksum) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto spcr = efi_config_table.AddAcpiTable((uint8_t *)kSpcrSignature);
  EXPECT_NE(spcr, nullptr);
  spcr->checksum ^= 1;
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), nullptr);
}

TEST(Acpi, LoadBySignatureSuccess) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto spcr = efi_config_table.AddAcpiTable((uint8_t *)kSpcrSignature);
  EXPECT_NE(spcr, nullptr);
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), spcr);
}

TEST(Acpi, SpcrTypeToKdrvNullInput) { EXPECT_EQ(spcr_type_to_kdrv(nullptr), (uint32_t)0); }

TEST(Acpi, SpcrTypeToKdrvRevision1) {
  acpi_spcr_t spcr = {
      .hdr =
          acpi_sdt_hdr_t{
              .revision = 1,
          },
  };
  EXPECT_EQ(spcr_type_to_kdrv(&spcr), (uint32_t)0);
}

TEST(Acpi, SpcrTypeToKdrvUnsupportedDevice) {
  acpi_spcr_t spcr = {
      .hdr =
          acpi_sdt_hdr_t{
              .revision = 3,
          },
      .interface_type = 0x0001,
  };
  EXPECT_EQ(spcr_type_to_kdrv(&spcr), (uint32_t)0);
}

TEST(Acpi, SpcrTypeToKdrvSuccess) {
  acpi_spcr_t spcr = {
      .hdr =
          acpi_sdt_hdr_t{
              .revision = 3,
          },
      .interface_type = 0x0003,
  };
  EXPECT_EQ(spcr_type_to_kdrv(&spcr), (uint32_t)KDRV_PL011_UART);
}

TEST(Acpi, UartDriverFromSpcrIrq) {
  acpi_spcr_t spcr = {
      .base_address =
          acpi_gas_t{
              .address = 0x80000,
          },
      .interrupt_type = 0x1,
      .irq = 33,
      .gsiv = 48,
  };
  dcfg_simple_t uart_driver;
  uart_driver_from_spcr(&spcr, &uart_driver);
  EXPECT_EQ(uart_driver.mmio_phys, (uint32_t)0x80000);
  EXPECT_EQ(uart_driver.irq, (uint32_t)33);
}

TEST(Acpi, UartDriverFromSpcrGsiv) {
  acpi_spcr_t spcr = {
      .base_address =
          acpi_gas_t{
              .address = 0x80000,
          },
      .interrupt_type = 0x10,
      .irq = 33,
      .gsiv = 48,
  };
  dcfg_simple_t uart_driver;
  uart_driver_from_spcr(&spcr, &uart_driver);
  EXPECT_EQ(uart_driver.mmio_phys, (uint32_t)0x80000);
  EXPECT_EQ(uart_driver.irq, (uint32_t)48);
}

}  // namespace
