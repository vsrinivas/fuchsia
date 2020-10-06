// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_STRUCTURES_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_STRUCTURES_H_

#include <lib/acpi_lite/internal.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// First byte and length of the x86 BIOS read-only area, [0xe0'000, 0xff'fff].
//
// Reference: ACPI v6.3, Section 5.2.5.1
constexpr zx_paddr_t kBiosReadOnlyAreaStart = 0xe0'000;
constexpr size_t kBiosReadOnlyAreaLength = 0x20'000;

// ACPI signature.
//
// Signatures are 4 byte ASCII strings. We represent them as an integer.
struct AcpiSignature {
  // Value, in big-endian format to match the in-memory representation.
  //
  // For example, on little-endian systems the signature "1234" will have a value
  // 0x34'33'32'31, with bytes reversed.
  uint32_t value;

  // Create an AcpiSignature from a C-style string.
  AcpiSignature() = default;
  explicit constexpr AcpiSignature(const char name[4])
      : value(acpi_lite::HostToBe32(name[0] << 24 | name[1] << 16 | name[2] << 8 | name[3])) {}

  // Operators.
  friend bool operator==(const AcpiSignature& left, const AcpiSignature& right) {
    return left.value == right.value;
  }
  friend bool operator!=(const AcpiSignature& left, const AcpiSignature& right) {
    return left.value != right.value;
  }

  // Write the signature into the given buffer.
  //
  // Buffer must have a length of at least 5.
  void WriteToBuffer(char* buffer) const;

  // Length of the signature when represented as ASCII.
  static constexpr int kAsciiLength = 4;
} __PACKED;

// Root System Description Pointer (RSDP)
//
// Reference: ACPI v6.3 Section 5.2.5.3.

struct AcpiRsdp {
  AcpiSignature sig1;  // "RSD "
  AcpiSignature sig2;  // "PTR "
  uint8_t checksum;
  uint8_t oemid[6];
  uint8_t revision;
  uint32_t rsdt_address;

  static constexpr auto kSignature1 = AcpiSignature("RSD ");
  static constexpr auto kSignature2 = AcpiSignature("PTR ");
} __PACKED;
static_assert(sizeof(AcpiRsdp) == 20);

struct AcpiRsdpV2 {
  // rev 1
  AcpiRsdp v1;

  // rev 2+
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} __PACKED;
static_assert(sizeof(AcpiRsdpV2) == 36);

// Standard system description table header, used as the header of
// multiple structures below.
//
// Reference: ACPI v6.3 Section 5.2.6.
struct AcpiSdtHeader {
  AcpiSignature sig;
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  uint8_t oemid[6];
  uint8_t oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} __PACKED;
static_assert(sizeof(AcpiSdtHeader) == 36);

// Root System Description Table (RSDT) and Extended System Description Table (XSDT)
//
// Reference: ACPI v6.3 Section 5.2.7 -- 5.2.8.
struct AcpiRsdt {
  AcpiSdtHeader header;

  // array of uint32s are placed immediately afterwards
  uint32_t addr32[0];

  static constexpr auto kSignature = AcpiSignature("RSDT");
} __PACKED;
static_assert(sizeof(AcpiRsdt) == 36);

struct AcpiXsdt {
  AcpiSdtHeader header;

  // array of uint64s are placed immediately afterwards
  uint32_t addr64[0];

  static constexpr auto kSignature = AcpiSignature("XSDT");
} __PACKED;
static_assert(sizeof(AcpiXsdt) == 36);

// ACPI Generic Address
//
// Reference: ACPI v6.3 Section 5.2.3.2
struct AcpiGenericAddress {
  uint8_t address_space_id;
  uint8_t register_bit_width;
  uint8_t register_bit_offset;
  uint8_t access_size;
  uint64_t address;
} __PACKED;
static_assert(sizeof(AcpiGenericAddress) == 12);

#define ACPI_ADDR_SPACE_MEMORY 0
#define ACPI_ADDR_SPACE_IO 1

// Multiple APIC Description Table
//
// The table is followed by interrupt control structures, each with
// a "AcpiSubTableHeader" header.
//
// Reference: ACPI v6.3 5.2.12.
struct AcpiMadtTable {
  AcpiSdtHeader header;

  uint32_t local_int_controller_address;
  uint32_t flags;

  static constexpr auto kSignature = AcpiSignature("APIC");
} __PACKED;
static_assert(sizeof(AcpiMadtTable) == 44);

struct AcpiSubTableHeader {
  uint8_t type;
  uint8_t length;
} __PACKED;
static_assert(sizeof(AcpiSubTableHeader) == 2);

// High Precision Event Timer Table
//
// Reference: IA-PC HPET (High Precision Event Timers) v1.0a, Section 3.2.4.
struct AcpiHpetTable {
  AcpiSdtHeader header;
  uint32_t id;
  AcpiGenericAddress address;
  uint8_t sequence;
  uint16_t minimum_tick;
  uint8_t flags;

  static constexpr auto kSignature = AcpiSignature("HPET");
} __PACKED;
static_assert(sizeof(AcpiHpetTable) == 56);

// SRAT table and descriptors.
//
// Reference: ACPI v6.3 Section 5.2.16.
struct AcpiSratTable {
  AcpiSdtHeader header;
  uint32_t _reserved;  // should be 1
  uint64_t _reserved2;

  static constexpr auto kSignature = AcpiSignature("SRAT");
} __PACKED;
static_assert(sizeof(AcpiSratTable) == 48);

// Type 0: processor local apic/sapic affinity structure
//
// Reference: ACPI v6.3 Section 5.2.16.1.
#define ACPI_SRAT_TYPE_PROCESSOR_AFFINITY 0
struct AcpiSratProcessorAffinityEntry {
  AcpiSubTableHeader header;
  uint8_t proximity_domain_low;
  uint8_t apic_id;
  uint32_t flags;
  uint8_t sapic_eid;
  uint8_t proximity_domain_high[3];
  uint32_t clock_domain;
} __PACKED;
static_assert(sizeof(AcpiSratProcessorAffinityEntry) == 16);

#define ACPI_SRAT_FLAG_ENABLED 1

// Type 1: memory affinity structure
//
// Reference: ACPI v6.3 Section 5.2.16.2.
#define ACPI_SRAT_TYPE_MEMORY_AFFINITY 1
struct AcpiSratMemoryAffinityEntry {
  AcpiSubTableHeader header;
  uint32_t proximity_domain;
  uint16_t _reserved;
  uint32_t base_address_low;
  uint32_t base_address_high;
  uint32_t length_low;
  uint32_t length_high;
  uint32_t _reserved2;
  uint32_t flags;
  uint32_t _reserved3;
  uint32_t _reserved4;
} __PACKED;
static_assert(sizeof(AcpiSratMemoryAffinityEntry) == 40);

// Type 2: processor x2apic affinity structure
//
// Reference: ACPI v6.3 Section 5.2.16.3.
#define ACPI_SRAT_TYPE_PROCESSOR_X2APIC_AFFINITY 2
struct AcpiSratProcessorX2ApicAffinityEntry {
  AcpiSubTableHeader header;
  uint16_t _reserved;
  uint32_t proximity_domain;
  uint32_t x2apic_id;
  uint32_t flags;
  uint32_t clock_domain;
  uint32_t _reserved2;
} __PACKED;
static_assert(sizeof(AcpiSratProcessorX2ApicAffinityEntry) == 24);

// Multiple APIC Description Table (MADT) entries.

// MADT entry type 0: Processor Local APIC (ACPI v6.3 Section 5.2.12.2)
#define ACPI_MADT_TYPE_LOCAL_APIC 0
struct AcpiMadtLocalApicEntry {
  AcpiSubTableHeader header;
  uint8_t processor_id;
  uint8_t apic_id;
  uint32_t flags;
} __PACKED;
static_assert(sizeof(AcpiMadtLocalApicEntry) == 8);

#define ACPI_MADT_FLAG_ENABLED 0x1

// MADT entry type 1: I/O APIC (ACPI v6.3 Section 5.2.12.3)
#define ACPI_MADT_TYPE_IO_APIC 1
struct AcpiMadtIoApicEntry {
  AcpiSubTableHeader header;
  uint8_t io_apic_id;
  uint8_t reserved;
  uint32_t io_apic_address;
  uint32_t global_system_interrupt_base;
} __PACKED;
static_assert(sizeof(AcpiMadtIoApicEntry) == 12);

// MADT entry type 2: Interrupt Source Override (ACPI v6.3 Section 5.2.12.5)
#define ACPI_MADT_TYPE_INT_SOURCE_OVERRIDE 2
struct AcpiMadtIntSourceOverrideEntry {
  AcpiSubTableHeader header;
  uint8_t bus;
  uint8_t source;
  uint32_t global_sys_interrupt;
  uint16_t flags;
} __PACKED;
static_assert(sizeof(AcpiMadtIntSourceOverrideEntry) == 10);

#define ACPI_MADT_FLAG_POLARITY_CONFORMS 0b00
#define ACPI_MADT_FLAG_POLARITY_HIGH 0b01
#define ACPI_MADT_FLAG_POLARITY_LOW 0b11
#define ACPI_MADT_FLAG_POLARITY_MASK 0b11

#define ACPI_MADT_FLAG_TRIGGER_CONFORMS 0b0000
#define ACPI_MADT_FLAG_TRIGGER_EDGE 0b0100
#define ACPI_MADT_FLAG_TRIGGER_LEVEL 0b1100
#define ACPI_MADT_FLAG_TRIGGER_MASK 0b1100

// DBG2 table
struct AcpiDbg2Table {
  AcpiSdtHeader header;
  uint32_t offset;
  uint32_t num_entries;

  static constexpr auto kSignature = AcpiSignature("DBG2");
} __PACKED;
static_assert(sizeof(AcpiDbg2Table) == 44);

struct AcpiDbg2Device {
  uint8_t revision;
  uint16_t length;
  uint8_t register_count;
  uint16_t namepath_length;
  uint16_t namepath_offset;
  uint16_t oem_data_length;
  uint16_t oem_data_offset;
  uint16_t port_type;
  uint16_t port_subtype;
  uint16_t reserved;
  uint16_t base_address_offset;
  uint16_t address_size_offset;
} __PACKED;
static_assert(sizeof(AcpiDbg2Device) == 22);

// debug port types
#define ACPI_DBG2_TYPE_SERIAL_PORT 0x8000
#define ACPI_DBG2_TYPE_1394_PORT 0x8001
#define ACPI_DBG2_TYPE_USB_PORT 0x8002
#define ACPI_DBG2_TYPE_NET_PORT 0x8003

// debug port subtypes
#define ACPI_DBG2_SUBTYPE_16550_COMPATIBLE 0x0000
#define ACPI_DBG2_SUBTYPE_16550_SUBSET 0x0001
#define ACPI_DBG2_SUBTYPE_1394_STANDARD 0x0000
#define ACPI_DBG2_SUBTYPE_USB_XHCI 0x0000
#define ACPI_DBG2_SUBTYPE_USB_EHCI 0x0001

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_STRUCTURES_H_
