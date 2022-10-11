// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_

#include <stdint.h>
#include <xefi.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>
#include <zircon/compiler.h>

#define ACPI_TABLE_SIGNATURE_SIZE 4

extern const efi_guid kAcpiTableGuid;
extern const efi_guid kAcpi20TableGuid;
extern const uint8_t kAcpiRsdpSignature[8];
extern const uint8_t kRsdtSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kXsdtSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kSpcrSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kFadtSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kMadtSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kGtdtSignature[ACPI_TABLE_SIGNATURE_SIZE];
extern const uint8_t kInterruptControllerTypeGicc;
extern const uint8_t kInterruptControllerTypeGicd;
extern const uint8_t kInterruptControllerTypeGicMsiFrame;
extern const uint8_t kInterruptControllerTypeGicr;
extern const uint64_t kGicv3rDefaultStride;
extern const uint8_t kPsciCompliant;
extern const uint8_t kPsciUseHvc;

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

typedef struct __attribute__((packed)) {
  uint8_t address_space_id;
  uint8_t register_bit_width;
  uint8_t register_bit_offset;
  uint8_t access_size;
  uint64_t address;
} acpi_gas_t;
_Static_assert(sizeof(acpi_gas_t) == 12, "GAS is the wrong size");

typedef struct __attribute__((packed)) {
  acpi_sdt_hdr_t hdr;
  uint8_t interface_type;
  uint8_t reserved[3];
  acpi_gas_t base_address;
  uint8_t interrupt_type;
  uint8_t irq;
  uint32_t gsiv;
  uint8_t baud_rate;
  uint8_t parity;
  uint8_t stop_bits;
  uint8_t flow_control;
  uint8_t terminal_type;
  uint8_t language;
  uint16_t pci_device_id;
  uint16_t pci_vendor_id;
  uint8_t pci_bus_number;
  uint8_t pci_device_number;
  uint8_t pci_function_number;
  uint32_t pci_flags;
  uint8_t pci_segment;
  uint32_t uart_clock_frequency;
} acpi_spcr_t;
_Static_assert(sizeof(acpi_spcr_t) == 80, "SPCR is the wrong size");

typedef struct __attribute__((packed)) {
  acpi_sdt_hdr_t hdr;
  uint32_t local_ic_address;
  uint32_t flags;
} acpi_madt_t;
_Static_assert(sizeof(acpi_madt_t) == 44, "MADT is the wrong size");

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t length;
  uint16_t reserved;
  uint32_t cpu_interface_number;
  uint32_t acpi_processor_uid;
  uint32_t flags;
  uint32_t parking_protocol_version;
  uint32_t performance_interrupt_gsiv;
  uint64_t parked_address;
  uint64_t physical_base_address;
  uint64_t gicv;
  uint64_t gich;
  uint32_t vgic_maintenance_interrupt;
  uint64_t gicr_base_address;
  uint64_t mpidr;
  uint8_t processor_power_class;
  uint8_t reserved2;
  uint16_t spe_overflow_interrupt;
} acpi_madt_gicc_t;
_Static_assert(sizeof(acpi_madt_gicc_t) == 80, "MADT GICC is the wrong size");

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t length;
  uint16_t reserved;
  uint32_t gic_id;
  uint64_t physical_base_address;
  uint32_t system_vector_base;
  uint8_t gic_version;
  uint8_t reserved2[3];
} acpi_madt_gicd_t;
_Static_assert(sizeof(acpi_madt_gicd_t) == 24, "MADT GICD is the wrong size");

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t length;
  uint16_t reserved;
  uint32_t gic_msi_frame_id;
  uint64_t physical_base_address;
  uint32_t flags;
  uint16_t spi_count;
  uint16_t spi_base;
} acpi_madt_gic_msi_t;
_Static_assert(sizeof(acpi_madt_gic_msi_t) == 24, "MADT GIC MSI is the wrong size");

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t length;
  uint16_t reserved;
  uint64_t discovery_range_base_address;
  uint32_t discovery_range_length;
} acpi_madt_gicr_t;
_Static_assert(sizeof(acpi_madt_gicr_t) == 16, "MADT GICR is the wrong size");

typedef struct __attribute__((packed)) {
  acpi_sdt_hdr_t hdr;
  uint32_t firmware_ctrl;
  uint32_t dsdt;
  uint8_t reserved;
  uint8_t preferred_pm_profile;
  uint16_t sci_int;
  uint32_t smi_cmd;
  uint8_t acpi_enable;
  uint8_t acpi_disable;
  uint8_t s4bios_req;
  uint8_t pstate_cnt;
  uint32_t pm1a_evt_blk;
  uint32_t pm1b_evt_blk;
  uint32_t pm1a_cnt_blk;
  uint32_t pm1b_cnt_blk;
  uint32_t pm2_cnt_blk;
  uint32_t pm_tmr_blk;
  uint32_t gpe0_blk;
  uint32_t gpe1_blk;
  uint8_t pm1_evt_len;
  uint8_t pm1_cnt_len;
  uint8_t pm2_cnt_len;
  uint8_t pm_tmr_len;
  uint8_t gpe0_blk_len;
  uint8_t gpe1_blk_len;
  uint8_t gpe1_base;
  uint8_t cst_cnt;
  uint16_t p_lvl2_lat;
  uint16_t p_lvl3_lat;
  uint16_t flush_size;
  uint16_t flush_stride;
  uint8_t duty_offset;
  uint8_t duty_width;
  uint8_t day_alrm;
  uint8_t mon_alrm;
  uint8_t century;
  uint16_t iapc_boot_arch;
  uint8_t reserved2;
  uint32_t flags;
  uint8_t reset_reg[12];
  uint8_t reset_value;
  uint16_t arm_boot_arch;
  uint8_t fadt_minor_version;
  uint64_t x_firmware_ctrl;
  uint64_t x_dsdt;
  acpi_gas_t x_pm1a_evt_blk;
  acpi_gas_t x_pm1b_evt_blk;
  acpi_gas_t x_pm1a_cnt_blk;
  acpi_gas_t x_pm1b_cnt_blk;
  acpi_gas_t x_pm2_cnt_blk;
  acpi_gas_t x_pm_tmr_blk;
  acpi_gas_t x_gpe0_blk;
  acpi_gas_t x_gpe1_blk;
  acpi_gas_t sleep_control_reg;
  acpi_gas_t sleep_status_reg;
  uint64_t hypervisor_vendory_identity;

} acpi_fadt_t;
_Static_assert(sizeof(acpi_fadt_t) == 276, "FADT is the wrong size");

typedef struct __attribute__((packed)) {
  acpi_sdt_hdr_t hdr;
  uint64_t cnt_control_base;
  uint32_t reserved;
  uint32_t secure_el1_timer_gsiv;
  uint32_t secure_el1_timer_flags;
  uint32_t nonsecure_el1_timer_gsiv;
  uint32_t nonsecure_el1_timer_flags;
  uint32_t virtual_el1_timer_gsiv;
  uint32_t virtual_el1_timer_flags;
  uint32_t el2_timer_gsiv;
  uint32_t el2_timer_flags;
  uint64_t cnt_read_base;
  uint32_t platform_timer_count;
  uint32_t platform_timer_offset;
  uint32_t virtual_el2_timer_gsiv;
  uint32_t virtual_el2_timer_flags;
} acpi_gtdt_t;
_Static_assert(sizeof(acpi_gtdt_t) == 104, "GTDT is the wrong size");

// Loads the Root System Description Pointer from UEFI.
// Returns NULL if UEFI contains no such entry in its configuration table.
acpi_rsdp_t* load_acpi_rsdp(const efi_configuration_table* entries, size_t num_entries);

// Loads an ACPI table with the given signature if it exists.
acpi_sdt_hdr_t* load_table_with_signature(acpi_rsdp_t* rsdp, uint8_t* signature);

// Translate SPCR serial interface types to Zircon kernel driver types.
// Returns 0 if a compatible Zircon UART driver is not found.
uint32_t spcr_type_to_kdrv(acpi_spcr_t* spcr);

// Convert data in an SPCR table into a UART kernel driver configuration.
void uart_driver_from_spcr(acpi_spcr_t* spcr, zbi_dcfg_simple_t* uart_driver);

// Use the data in the MADT table to construct a CPU topology.
// Returns the number of cores found, 0 if there are no supported cores.
uint8_t topology_from_madt(const acpi_madt_t* madt, zbi_topology_node_t* nodes, size_t max_nodes);

// Use the data in the MADT table to construct a GIC configuration.
// Returns the version of the GIC that was found, 0 if there was an error.
uint8_t gic_driver_from_madt(const acpi_madt_t* madt, zbi_dcfg_arm_gic_v2_driver_t* v2_cfg,
                             zbi_dcfg_arm_gic_v3_driver_t* v3_cfg);

// Uses the data in the FADT table to construct a PSCI configuration.
// Returns -1 if the architecture does not support PSCI.
// Note that this currently only sets the use_hvc field of the PSCI driver.
int psci_driver_from_fadt(const acpi_fadt_t* fadt, zbi_dcfg_arm_psci_driver_t* cfg);

// Uses the data in the GTDT table to construct an ARM generic timer
// configuration.
void timer_from_gtdt(const acpi_gtdt_t* gtdt, zbi_dcfg_arm_generic_timer_driver_t* timer);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_ACPI_H_
