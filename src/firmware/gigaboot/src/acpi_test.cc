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
  acpi_spcr_t *AddSpcrTable();
  acpi_madt_t *AddMadtTable();
  void AddInterruptControllerToMadt(acpi_madt_t *madt, void *controller, size_t size);

  efi_configuration_table *RawTable();

 private:
  uint8_t rsdp_position_;
  std::vector<efi_configuration_table> table_;

  int AddPointerToXsdt(uint64_t addr);
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
    delete[] (uint8_t *)xsdt_.entries[i];
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

void EfiConfigTable::AddInterruptControllerToMadt(acpi_madt_t *madt, void *controller,
                                                  size_t size) {
  uint8_t *next_table_start = (uint8_t *)madt + madt->hdr.length;
  madt->hdr.length += size;
  memcpy(next_table_start, controller, size);
  update_checksum(madt, madt->hdr.length, madt->hdr.checksum);
}

acpi_madt_t *EfiConfigTable::AddMadtTable() {
  // We allocate extra space for the interrupt controller structures.
  auto buffer = new uint8_t[512];
  auto madt = (acpi_madt_t *)buffer;
  madt->hdr.length = sizeof(acpi_madt_t);
  memcpy(&madt->hdr.signature, kMadtSignature, sizeof(kMadtSignature));
  update_checksum(madt, madt->hdr.length, madt->hdr.checksum);
  if (AddPointerToXsdt((uint64_t)madt)) {
    return nullptr;
  }
  return madt;
}

acpi_spcr_t *EfiConfigTable::AddSpcrTable() {
  auto buffer = new uint8_t[sizeof(acpi_spcr_t)];
  auto spcr = (acpi_spcr_t *)buffer;
  spcr->hdr.length = sizeof(acpi_spcr_t);
  memcpy(&spcr->hdr.signature, kSpcrSignature, sizeof(kSpcrSignature));
  update_checksum(spcr, spcr->hdr.length, spcr->hdr.checksum);
  if (AddPointerToXsdt((uint64_t)spcr)) {
    return nullptr;
  }
  return spcr;
}

int EfiConfigTable::AddPointerToXsdt(uint64_t addr) {
  auto num_entries = (xsdt_.hdr.length - sizeof(acpi_sdt_hdr_t)) / sizeof(uint64_t);
  if (num_entries >= sizeof(xsdt_.entries) / sizeof(uint64_t)) {
    return -1;
  }
  xsdt_.entries[num_entries] = addr;
  xsdt_.hdr.length += sizeof(uint64_t);
  update_checksum(&xsdt_.hdr, xsdt_.hdr.length, xsdt_.hdr.checksum);

  return 0;
}

efi_configuration_table *EfiConfigTable::RawTable() { return &table_[0]; }

// Checks if the given topologies are equal. Returns 0 if equal, 1 if not.
// Currently only checks equality for ARM, as the x86 path isn't exercised.
void check_topology_eq(zbi_topology_node_t *got, zbi_topology_node_t *want) {
  ASSERT_EQ(got->entity_type, want->entity_type);
  ASSERT_EQ(got->parent_index, want->parent_index);
  ASSERT_EQ(got->entity.processor.logical_id_count, want->entity.processor.logical_id_count);
  for (uint8_t i = 0; i < got->entity.processor.logical_id_count; i++) {
    ASSERT_EQ(got->entity.processor.logical_ids[i], want->entity.processor.logical_ids[i]);
  }
  ASSERT_EQ(got->entity.processor.flags, want->entity.processor.flags);
  ASSERT_EQ(got->entity.processor.architecture, want->entity.processor.architecture);
  zbi_topology_arm_info_t got_arm_info = got->entity.processor.architecture_info.arm;
  zbi_topology_arm_info_t want_arm_info = want->entity.processor.architecture_info.arm;
  ASSERT_EQ(got_arm_info.cluster_1_id, want_arm_info.cluster_1_id);
  ASSERT_EQ(got_arm_info.cluster_2_id, want_arm_info.cluster_2_id);
  ASSERT_EQ(got_arm_info.cluster_3_id, want_arm_info.cluster_3_id);
  ASSERT_EQ(got_arm_info.cpu_id, want_arm_info.cpu_id);
  ASSERT_EQ(got_arm_info.gic_id, want_arm_info.gic_id);
}

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
  efi_config_table.AddSpcrTable();
  efi_config_table.CorruptXsdtSignature();
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), nullptr);
}

TEST(Acpi, LoadBySignatureInvalidXsdtChecksum) {
  auto efi_config_table = EfiConfigTable(2, 0);
  efi_config_table.AddSpcrTable();
  efi_config_table.CorruptXsdtChecksum();
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), nullptr);
}

TEST(Acpi, LoadBySignatureTableNotFound) {
  auto efi_config_table = EfiConfigTable(2, 0);
  EXPECT_NE(efi_config_table.AddMadtTable(), nullptr);
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), nullptr);
}

TEST(Acpi, LoadBySignatureInvalidTableChecksum) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto spcr = efi_config_table.AddSpcrTable();
  EXPECT_NE(spcr, nullptr);
  spcr->hdr.checksum ^= 1;
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature), nullptr);
}

TEST(Acpi, LoadBySignatureSuccess) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto spcr = efi_config_table.AddSpcrTable();
  EXPECT_NE(spcr, nullptr);
  EXPECT_EQ(load_table_with_signature(&efi_config_table.rsdp_, (uint8_t *)kSpcrSignature),
            &spcr->hdr);
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
  EXPECT_EQ(spcr_type_to_kdrv(&spcr), (uint32_t)ZBI_KERNEL_DRIVER_PL011_UART);
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
  zbi_dcfg_simple_t uart_driver;
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
  zbi_dcfg_simple_t uart_driver;
  uart_driver_from_spcr(&spcr, &uart_driver);
  EXPECT_EQ(uart_driver.mmio_phys, (uint32_t)0x80000);
  EXPECT_EQ(uart_driver.irq, (uint32_t)48);
}

TEST(Acpi, TopologyFromMadtTooManyCpus) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  // Construct a dual core system.
  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  auto gicc1 = acpi_madt_gicc_t{
      .type = kInterruptControllerTypeGicc,
      .length = sizeof(acpi_madt_gicc_t),
      .cpu_interface_number = 0xf,
      .mpidr = 0x4000030201,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicc1, sizeof(gicc1));

  auto gicc2 = acpi_madt_gicc_t{
      .type = kInterruptControllerTypeGicc,
      .length = sizeof(acpi_madt_gicc_t),
      .cpu_interface_number = 0x8,
      .mpidr = 0x26001a0703,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicc2, sizeof(gicc2));

  // Parse the CPU topology.
  zbi_topology_node_t nodes[1];
  auto num_nodes = topology_from_madt(madt, nodes, 1);
  EXPECT_EQ(num_nodes, 1);

  zbi_topology_node_t expected[1] = {
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {0},
                          .logical_id_count = 1,
                          .flags = ZBI_TOPOLOGY_PROCESSOR_PRIMARY,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0x2,
                                          .cluster_2_id = 0x3,
                                          .cluster_3_id = 0x40,
                                          .cpu_id = 0x1,
                                          .gic_id = 0xf,
                                      },
                              },
                      },
              },
      },
  };
  check_topology_eq(&expected[0], &nodes[0]);
}

TEST(Acpi, TopologyFromMadtSuccess) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  // Construct a dual core system.
  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  auto gicc1 = acpi_madt_gicc_t{
      .type = kInterruptControllerTypeGicc,
      .length = sizeof(acpi_madt_gicc_t),
      .cpu_interface_number = 0xf,
      .mpidr = 0x4000030201,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicc1, sizeof(gicc1));

  auto gicc2 = acpi_madt_gicc_t{
      .type = kInterruptControllerTypeGicc,
      .length = sizeof(acpi_madt_gicc_t),
      .cpu_interface_number = 0x8,
      .mpidr = 0x26001a0703,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicc2, sizeof(gicc2));

  // Parse the CPU topology from that MADT.
  const uint8_t expected_num_nodes = 2;
  zbi_topology_node_t nodes[expected_num_nodes];
  auto num_nodes = topology_from_madt(madt, nodes, expected_num_nodes);
  EXPECT_EQ(num_nodes, expected_num_nodes);

  zbi_topology_node_t expected[expected_num_nodes] = {
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {0},
                          .logical_id_count = 1,
                          .flags = ZBI_TOPOLOGY_PROCESSOR_PRIMARY,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0x2,
                                          .cluster_2_id = 0x3,
                                          .cluster_3_id = 0x40,
                                          .cpu_id = 0x1,
                                          .gic_id = 0xf,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {1},
                          .logical_id_count = 1,
                          .flags = 0,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0x7,
                                          .cluster_2_id = 0x1a,
                                          .cluster_3_id = 0x26,
                                          .cpu_id = 0x3,
                                          .gic_id = 0x8,
                                      },
                              },
                      },
              },
      },
  };
  for (int i = 0; i < expected_num_nodes; i++) {
    check_topology_eq(&expected[i], &nodes[i]);
  }
}

TEST(Acpi, GicDriverFromMadtNoGicd) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  auto gicc1 = acpi_madt_gicc_t{
      .type = kInterruptControllerTypeGicc,
      .length = sizeof(acpi_madt_gicc_t),
      .cpu_interface_number = 0xf,
      .mpidr = 0x4000030201,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicc1, sizeof(gicc1));

  zbi_dcfg_arm_gic_v2_driver_t v2;
  zbi_dcfg_arm_gic_v3_driver_t v3;
  EXPECT_EQ(gic_driver_from_madt(madt, &v2, &v3), 0);
}

TEST(Acpi, GicDriverFromMadtV2NoGicc) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
      .gic_version = 0x2,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  auto gic_msi = acpi_madt_gic_msi_t{
      .type = kInterruptControllerTypeGicMsiFrame,
      .length = sizeof(acpi_madt_gic_msi_t),
      .physical_base_address = 0x40000,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gic_msi, sizeof(gic_msi));

  zbi_dcfg_arm_gic_v2_driver_t v2;
  zbi_dcfg_arm_gic_v3_driver_t v3;
  EXPECT_EQ(gic_driver_from_madt(madt, &v2, &v3), 0);
}

TEST(Acpi, GicDriverFromMadtV2NoGicMsi) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
      .physical_base_address = 0x30000,
      .gic_version = 0x2,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  auto gicc = acpi_madt_gicc_t{
      .type = kInterruptControllerTypeGicc,
      .length = sizeof(acpi_madt_gicc_t),
      .cpu_interface_number = 0xf,
      .physical_base_address = 0x10000,
      .mpidr = 0x4000030201,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicc, sizeof(gicc));

  zbi_dcfg_arm_gic_v2_driver_t v2;
  zbi_dcfg_arm_gic_v3_driver_t v3;
  EXPECT_EQ(gic_driver_from_madt(madt, &v2, &v3), 2);

  zbi_dcfg_arm_gic_v2_driver_t expected = {
      .mmio_phys = 0x10000,
      .msi_frame_phys = 0x0,
      .gicd_offset = 0x20000,
      .gicc_offset = 0x0,
      .ipi_base = 0,
      .optional = true,
      .use_msi = false,
  };
  ASSERT_EQ(memcmp(&expected, &v2, sizeof(expected)), 0);
}

TEST(Acpi, GicDriverFromMadtV2GiccBase) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  auto gicc = acpi_madt_gicc_t{
      .type = kInterruptControllerTypeGicc,
      .length = sizeof(acpi_madt_gicc_t),
      .cpu_interface_number = 0xf,
      .physical_base_address = 0x10000,
      .mpidr = 0x4000030201,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicc, sizeof(gicc));

  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
      .physical_base_address = 0x30000,
      .gic_version = 0x2,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  auto gic_msi = acpi_madt_gic_msi_t{
      .type = kInterruptControllerTypeGicMsiFrame,
      .length = sizeof(acpi_madt_gic_msi_t),
      .physical_base_address = 0x40000,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gic_msi, sizeof(gic_msi));

  zbi_dcfg_arm_gic_v2_driver_t v2;
  zbi_dcfg_arm_gic_v3_driver_t v3;
  EXPECT_EQ(gic_driver_from_madt(madt, &v2, &v3), 2);

  zbi_dcfg_arm_gic_v2_driver_t expected = {
      .mmio_phys = 0x10000,
      .msi_frame_phys = 0x40000,
      .gicd_offset = 0x20000,
      .gicc_offset = 0x0,
      .ipi_base = 0,
      .optional = true,
      .use_msi = true,
  };
  ASSERT_EQ(memcmp(&expected, &v2, sizeof(zbi_dcfg_arm_gic_v2_driver_t)), 0);
}

TEST(Acpi, GicDriverFromMadtV2GicdBase) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  auto gicc = acpi_madt_gicc_t{
      .type = kInterruptControllerTypeGicc,
      .length = sizeof(acpi_madt_gicc_t),
      .cpu_interface_number = 0xf,
      .physical_base_address = 0x30000,
      .mpidr = 0x4000030201,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicc, sizeof(gicc));

  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
      .physical_base_address = 0x20000,
      .gic_version = 0x2,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  auto gic_msi = acpi_madt_gic_msi_t{
      .type = kInterruptControllerTypeGicMsiFrame,
      .length = sizeof(acpi_madt_gic_msi_t),
      .physical_base_address = 0x40000,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gic_msi, sizeof(gic_msi));

  zbi_dcfg_arm_gic_v2_driver_t v2;
  zbi_dcfg_arm_gic_v3_driver_t v3;
  EXPECT_EQ(gic_driver_from_madt(madt, &v2, &v3), 2);

  zbi_dcfg_arm_gic_v2_driver_t expected = {
      .mmio_phys = 0x20000,
      .msi_frame_phys = 0x40000,
      .gicd_offset = 0x0,
      .gicc_offset = 0x10000,
      .ipi_base = 0,
      .optional = true,
      .use_msi = true,
  };
  ASSERT_EQ(memcmp(&expected, &v2, sizeof(zbi_dcfg_arm_gic_v2_driver_t)), 0);
}

TEST(Acpi, GicDriverFromMadtV3NoGicr) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
      .gic_version = 0x3,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  zbi_dcfg_arm_gic_v2_driver_t v2;
  zbi_dcfg_arm_gic_v3_driver_t v3;
  EXPECT_EQ(gic_driver_from_madt(madt, &v2, &v3), 0);
}

TEST(Acpi, GicDriverFromMadtV3GicdBase) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
      .physical_base_address = 0x20000,
      .gic_version = 0x3,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  auto gicr = acpi_madt_gicr_t{
      .type = kInterruptControllerTypeGicr,
      .length = sizeof(acpi_madt_gicr_t),
      .discovery_range_base_address = 0xf0000,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicr, sizeof(gicr));

  zbi_dcfg_arm_gic_v2_driver_t v2;
  zbi_dcfg_arm_gic_v3_driver_t v3;
  EXPECT_EQ(gic_driver_from_madt(madt, &v2, &v3), 3);

  zbi_dcfg_arm_gic_v3_driver_t expected = {
      .mmio_phys = 0x20000,
      .gicd_offset = 0x0,
      .gicr_offset = 0xd0000,
      .gicr_stride = kGicv3rDefaultStride,
      .ipi_base = 0,
      .optional = true,
  };
  ASSERT_EQ(memcmp(&expected, &v3, sizeof(zbi_dcfg_arm_gic_v3_driver_t)), 0);
}

TEST(Acpi, GicDriverFromMadtV3GicrBase) {
  auto efi_config_table = EfiConfigTable(2, 0);
  auto madt = (acpi_madt_t *)efi_config_table.AddMadtTable();
  EXPECT_NE(madt, nullptr);

  auto gicd = acpi_madt_gicd_t{
      .type = kInterruptControllerTypeGicd,
      .length = sizeof(acpi_madt_gicd_t),
      .physical_base_address = 0x80000,
      .gic_version = 0x3,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicd, sizeof(gicd));

  auto gicr = acpi_madt_gicr_t{
      .type = kInterruptControllerTypeGicr,
      .length = sizeof(acpi_madt_gicr_t),
      .discovery_range_base_address = 0x10000,
  };
  efi_config_table.AddInterruptControllerToMadt(madt, &gicr, sizeof(gicr));

  zbi_dcfg_arm_gic_v2_driver_t v2;
  zbi_dcfg_arm_gic_v3_driver_t v3;
  EXPECT_EQ(gic_driver_from_madt(madt, &v2, &v3), 3);

  zbi_dcfg_arm_gic_v3_driver_t expected = {
      .mmio_phys = 0x10000,
      .gicd_offset = 0x70000,
      .gicr_offset = 0x0,
      .gicr_stride = kGicv3rDefaultStride,
      .ipi_base = 0,
      .optional = true,
  };
  ASSERT_EQ(memcmp(&expected, &v3, sizeof(zbi_dcfg_arm_gic_v3_driver_t)), 0);
}

TEST(Acpi, PsciDriverFromFadtNotPsciCompliant) {
  auto fadt = acpi_fadt_t{};
  zbi_dcfg_arm_psci_driver_t cfg;
  EXPECT_EQ(psci_driver_from_fadt(&fadt, &cfg), -1);
}

TEST(Acpi, PsciDriverFromFadtNoHvc) {
  auto fadt = acpi_fadt_t{
      .arm_boot_arch = (uint16_t)kPsciCompliant,
  };
  zbi_dcfg_arm_psci_driver_t cfg;
  EXPECT_EQ(psci_driver_from_fadt(&fadt, &cfg), 0);
  EXPECT_EQ(cfg.use_hvc, 0);
}

TEST(Acpi, PsciDriverFromFadtUseHvc) {
  auto fadt = acpi_fadt_t{
      .arm_boot_arch = (uint16_t)(kPsciCompliant | kPsciUseHvc),
  };
  zbi_dcfg_arm_psci_driver_t cfg;
  EXPECT_EQ(psci_driver_from_fadt(&fadt, &cfg), 0);
  EXPECT_NE(cfg.use_hvc, 0);
}

TEST(Acpi, TimerDriverFromGtdt) {
  auto gtdt = acpi_gtdt_t{
      .nonsecure_el1_timer_gsiv = 30,
      .virtual_el1_timer_gsiv = 27,
  };
  zbi_dcfg_arm_generic_timer_driver_t timer;
  timer_from_gtdt(&gtdt, &timer);
  EXPECT_EQ(timer.irq_phys, (uint32_t)30);
  EXPECT_EQ(timer.irq_virt, (uint32_t)27);
}

}  // namespace
