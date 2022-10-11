// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xefi.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>

const efi_guid kAcpiTableGuid = ACPI_TABLE_GUID;
const efi_guid kAcpi20TableGuid = ACPI_20_TABLE_GUID;
const uint8_t kAcpiRsdpSignature[8] = "RSD PTR ";
const uint8_t kRsdtSignature[ACPI_TABLE_SIGNATURE_SIZE] = "RSDT";
const uint8_t kXsdtSignature[ACPI_TABLE_SIGNATURE_SIZE] = "XSDT";
const uint8_t kSpcrSignature[ACPI_TABLE_SIGNATURE_SIZE] = "SPCR";
const uint8_t kMadtSignature[ACPI_TABLE_SIGNATURE_SIZE] = "APIC";
const uint8_t kFadtSignature[ACPI_TABLE_SIGNATURE_SIZE] = "FACP";
const uint8_t kGtdtSignature[ACPI_TABLE_SIGNATURE_SIZE] = "GTDT";
const uint8_t kInterruptControllerTypeGicc = 0xb;
const uint8_t kInterruptControllerTypeGicd = 0xc;
const uint8_t kInterruptControllerTypeGicMsiFrame = 0xd;
const uint8_t kInterruptControllerTypeGicr = 0xe;
// The ARM GICv3 spec states that 0x20000 is the default GICR stride.
const uint64_t kGicv3rDefaultStride = 0x20000;
const uint8_t kPsciCompliant = 0x1;
const uint8_t kPsciUseHvc = 0x2;

// Returns the minimum of the two 64 bit physical addresses.
uint64_t min(uint64_t first, uint64_t second) {
  if (first < second) {
    return first;
  }
  return second;
}

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

acpi_rsdp_t* load_acpi_rsdp(const efi_configuration_table* entries, size_t num_entries) {
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
      return ZBI_KERNEL_DRIVER_PL011_UART;
    default:
      printf("unsupported serial interface type 0x%x\n", spcr->interface_type);
      return 0;
  }
}

void uart_driver_from_spcr(acpi_spcr_t* spcr, zbi_dcfg_simple_t* uart_driver) {
  memset(uart_driver, 0x0, sizeof(zbi_dcfg_simple_t));
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

uint8_t topology_from_madt(const acpi_madt_t* madt, zbi_topology_node_t* nodes, size_t max_nodes) {
  memset(nodes, 0x0, sizeof(zbi_topology_node_t));
  const uint8_t* madt_end = (uint8_t*)madt + madt->hdr.length;
  // The list of interrupt controller structures is located at the end of MADT,
  // and each one starts with a type and a length.
  typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
  } interrupt_controller_hdr_t;
  const interrupt_controller_hdr_t* current_entry = (interrupt_controller_hdr_t*)(madt + 1);
  uint8_t num_nodes = 0;
  while ((uint8_t*)current_entry < madt_end) {
    if (current_entry->type == kInterruptControllerTypeGicc) {
      // The given buffer of ZBI topology nodes was not long enough to contain
      // the entire topology, so return early with the number we could fit.
      if (num_nodes >= max_nodes) {
        return num_nodes;
      }

      // The GICC table contains the multiprocesser affinity register (MPIDR)
      // for each core. We can use the contents of this register to construct
      // the CPU topology (on ARM).
      const acpi_madt_gicc_t* gicc = (acpi_madt_gicc_t*)current_entry;
      nodes[num_nodes] = (zbi_topology_node_t){
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {num_nodes},
                          .logical_id_count = 1,
                          .flags = (num_nodes == 0) ? ZBI_TOPOLOGY_PROCESSOR_PRIMARY : 0,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          // aff1
                                          .cluster_1_id = (gicc->mpidr >> 8) & 0xFF,
                                          // aff2
                                          .cluster_2_id = (gicc->mpidr >> 16) & 0xFF,
                                          // aff3
                                          .cluster_3_id = (gicc->mpidr >> 32) & 0xFF,
                                          // aff0
                                          .cpu_id = gicc->mpidr & 0xFF,
                                          .gic_id = (uint8_t)gicc->cpu_interface_number,
                                      },
                              },
                      },
              },
      };
      num_nodes++;
    }
    current_entry =
        (interrupt_controller_hdr_t*)(((uint8_t*)current_entry) + current_entry->length);
  }
  return num_nodes;
}

uint8_t gic_driver_from_madt(const acpi_madt_t* madt, zbi_dcfg_arm_gic_v2_driver_t* v2_cfg,
                             zbi_dcfg_arm_gic_v3_driver_t* v3_cfg) {
  memset(v2_cfg, 0x0, sizeof(zbi_dcfg_arm_gic_v2_driver_t));
  memset(v3_cfg, 0x0, sizeof(zbi_dcfg_arm_gic_v3_driver_t));
  const uint8_t* madt_end = (uint8_t*)madt + madt->hdr.length;
  // The list of interrupt controller structures is located at the end of MADT,
  // and each one starts with a type and a length.
  typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
  } interrupt_controller_hdr_t;
  const interrupt_controller_hdr_t* current_entry = (interrupt_controller_hdr_t*)(madt + 1);

  // Assemble the correct set of interrupt controller structures needed to
  // construct a GIC configuration.
  acpi_madt_gicc_t* gicc = NULL;
  acpi_madt_gicd_t* gicd = NULL;
  acpi_madt_gic_msi_t* gic_msi = NULL;
  acpi_madt_gicr_t* gicr = NULL;
  while ((uint8_t*)current_entry < madt_end) {
    switch (current_entry->type) {
      case kInterruptControllerTypeGicc:
        gicc = (acpi_madt_gicc_t*)current_entry;
        break;
      case kInterruptControllerTypeGicd:
        gicd = (acpi_madt_gicd_t*)current_entry;
        break;
      case kInterruptControllerTypeGicr:
        gicr = (acpi_madt_gicr_t*)current_entry;
        break;
      case kInterruptControllerTypeGicMsiFrame:
        gic_msi = (acpi_madt_gic_msi_t*)current_entry;
        break;
    }
    current_entry =
        (interrupt_controller_hdr_t*)(((uint8_t*)current_entry) + current_entry->length);
  }

  // GICD structures are required whenever utilizing a GIC, so return early if
  // one wasn't found.
  if (gicd == NULL) {
    printf("GICD structure was not found\n");
    return 0;
  }
  switch (gicd->gic_version) {
    case 0x02:
      if (gicc == NULL) {
        printf("GICC structure was not found\n");
        return 0;
      }
      v2_cfg->mmio_phys = min(gicc->physical_base_address, gicd->physical_base_address);
      v2_cfg->gicc_offset = gicc->physical_base_address - v2_cfg->mmio_phys;
      v2_cfg->gicd_offset = gicd->physical_base_address - v2_cfg->mmio_phys;
      if (gic_msi != NULL) {
        v2_cfg->use_msi = true;
        v2_cfg->msi_frame_phys = gic_msi->physical_base_address;
      }
      v2_cfg->ipi_base = 0;
      v2_cfg->optional = true;
      break;
    case 0x03:
      if (gicr == NULL) {
        printf("GICR structure was not found\n");
        return 0;
      }
      v3_cfg->mmio_phys = min(gicr->discovery_range_base_address, gicd->physical_base_address);
      v3_cfg->gicr_offset = gicr->discovery_range_base_address - v3_cfg->mmio_phys;
      v3_cfg->gicd_offset = gicd->physical_base_address - v3_cfg->mmio_phys;
      v3_cfg->gicr_stride = kGicv3rDefaultStride;
      v3_cfg->ipi_base = 0;
      v3_cfg->optional = true;
      break;
  }
  return gicd->gic_version;
}

int psci_driver_from_fadt(const acpi_fadt_t* fadt, zbi_dcfg_arm_psci_driver_t* cfg) {
  memset(cfg, 0x0, sizeof(zbi_dcfg_arm_psci_driver_t));
  if ((fadt->arm_boot_arch & kPsciCompliant) == 0) {
    return -1;
  }
  cfg->use_hvc = fadt->arm_boot_arch & kPsciUseHvc;
  return 0;
}

void timer_from_gtdt(const acpi_gtdt_t* gtdt, zbi_dcfg_arm_generic_timer_driver_t* timer) {
  memset(timer, 0x0, sizeof(zbi_dcfg_arm_generic_timer_driver_t));
  timer->irq_phys = gtdt->nonsecure_el1_timer_gsiv;
  timer->irq_virt = gtdt->virtual_el1_timer_gsiv;
}
