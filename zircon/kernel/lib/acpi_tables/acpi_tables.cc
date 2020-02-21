// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <lib/acpi_tables.h>
#include <trace.h>

#include <fbl/span.h>
#include <lk/init.h>

#define LOCAL_TRACE 0

namespace {

constexpr uint32_t kAcpiMaxInitTables = 32;
ACPI_TABLE_DESC acpi_tables[kAcpiMaxInitTables];

// Read a POD struct of type "T" from memory at "data" with a given
// offset.
//
// Return the struct, and a span of the data which may be larger than
// sizeof(T).
//
// Return an error if the span is not large enough to contain the data.
template <typename T>
inline zx_status_t ReadStruct(fbl::Span<const uint8_t> data, T* out, size_t offset = 0) {
  // Ensure there is enough data for the header.
  if (data.size_bytes() < offset + sizeof(T)) {
    return ZX_ERR_INTERNAL;
  }

  // Copy the data to the out pointer.
  //
  // We copy the memory instead of just returning a raw pointer to the
  // input data to avoid unaligned memory accesses, which are undefined
  // behaviour in C (albeit, on x86 don't tend to matter in practice).
  memcpy(reinterpret_cast<uint8_t*>(out), data.data() + offset, sizeof(T));

  return ZX_OK;
}

// Read a POD struct of type "T" from memory at "data", which has a length field "F".
//
// Return the struct, and a span of the data which may be larger than
// sizeof(T).
//
// Return an error if the span is not large enough to contain the data.
template <typename T, typename F>
inline zx_status_t ReadVariableLengthStruct(fbl::Span<const uint8_t> data, F T::*length_field,
                                            T* out, fbl::Span<const uint8_t>* payload,
                                            size_t offset = 0) {
  // Read the struct data.
  zx_status_t status = ReadStruct(data, out, offset);
  if (status != ZX_OK) {
    return status;
  }

  // Ensure the input data is large enough to fit the data in
  // "length_field".
  auto length = static_cast<size_t>(out->*length_field);
  if (length + offset > data.size_bytes()) {
    return ZX_ERR_INTERNAL;
  }

  // Create a span of the data.
  *payload = fbl::Span<const uint8_t>(data.begin() + offset, out->*length_field);

  return ZX_OK;
}

// Read an ACPI table entry of type "T" from the memory starting a "header".
//
// On success, we return the struct and a span refering to the range of
// data, which may be larger than sizeof(T).
//
// We assume that the memory being pointed contains at least sizeof(T)
// bytes. Return an error if the header isn't valid.
template <typename T>
inline zx_status_t ReadAcpiEntry(const ACPI_TABLE_HEADER* header, T* out,
                                 fbl::Span<const uint8_t>* payload) {
  // Read the length. Use "memcpy" to avoid unaligned reads.
  uint32_t length;
  memcpy(&length, &header->Length, sizeof(uint32_t));
  if (length < sizeof(T)) {
    return ZX_ERR_INTERNAL;
  }

  // Ensure the table doesn't wrap the address space.
  auto start = reinterpret_cast<uintptr_t>(header);
  auto end = start + header->Length;
  if (end < start) {
    return ZX_ERR_INTERNAL;
  }

  // Ensure that the header length looks reasonable.
  if (header->Length > 16 * 1024) {
    TRACEF("Table entry suspiciously long: %u\n", header->Length);
    return ZX_ERR_INTERNAL;
  }

  // Read the result.
  *payload = fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(header), length);
  return ReadStruct(*payload, out);
}

}  // namespace

bool AcpiTables::initialized_ = false;

void AcpiTables::Initialize(uint32_t) {
  DEBUG_ASSERT(!initialized_);

  const auto status = AcpiInitializeTables(acpi_tables, kAcpiMaxInitTables, FALSE);

  if (status == AE_NOT_FOUND) {
    TRACEF("WARNING: could not find ACPI tables\n");
    return;
  } else if (status == AE_NO_MEMORY) {
    TRACEF("WARNING: could not initialize ACPI tables, no memory\n");
    return;
  } else if (status != AE_OK) {
    TRACEF("WARNING: could not initialize ACPI tables for unknown reason\n");
    return;
  }

  initialized_ = true;
  LTRACEF("ACPI tables initialized\n");
}

/* initialize ACPI tables as soon as we have a working VM */
LK_INIT_HOOK(acpi_tables, &AcpiTables::Initialize, LK_INIT_LEVEL_VM + 1)

zx_status_t AcpiTables::cpu_count(uint32_t* cpu_count) const {
  uint32_t count = 0;
  auto visitor = [&count](ACPI_SUBTABLE_HEADER* record) {
    ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)record;
    if (!(lapic->LapicFlags & ACPI_MADT_ENABLED)) {
      LTRACEF("Skipping disabled processor %02x\n", lapic->Id);
      return ZX_OK;
    }

    count++;
    return ZX_OK;
  };

  auto status = ForEachInMadt(ACPI_MADT_TYPE_LOCAL_APIC, visitor);
  if (status != ZX_OK) {
    return status;
  }

  *cpu_count = count;
  return ZX_OK;
}

zx_status_t AcpiTables::cpu_apic_ids(uint32_t* apic_ids, uint32_t array_size,
                                     uint32_t* apic_id_count) const {
  uint32_t count = 0;
  auto visitor = [apic_ids, array_size, &count](ACPI_SUBTABLE_HEADER* record) {
    ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)record;
    if (!(lapic->LapicFlags & ACPI_MADT_ENABLED)) {
      LTRACEF("Skipping disabled processor %02x\n", lapic->Id);
      return ZX_OK;
    }

    if (count >= array_size) {
      return ZX_ERR_INVALID_ARGS;
    }
    apic_ids[count++] = lapic->Id;
    return ZX_OK;
  };

  auto status = ForEachInMadt(ACPI_MADT_TYPE_LOCAL_APIC, visitor);
  if (status != ZX_OK) {
    return status;
  }

  *apic_id_count = count;
  return ZX_OK;
}

zx_status_t AcpiTables::io_apic_count(uint32_t* io_apics_count) const {
  return NumInMadt(ACPI_MADT_TYPE_IO_APIC, io_apics_count);
}

zx_status_t AcpiTables::io_apics(io_apic_descriptor* io_apics, uint32_t array_size,
                                 uint32_t* io_apics_count) const {
  uint32_t count = 0;
  auto visitor = [io_apics, array_size, &count](ACPI_SUBTABLE_HEADER* record) {
    ACPI_MADT_IO_APIC* io_apic = (ACPI_MADT_IO_APIC*)record;
    if (count >= array_size) {
      return ZX_ERR_INVALID_ARGS;
    }
    io_apics[count].apic_id = io_apic->Id;
    io_apics[count].paddr = io_apic->Address;
    io_apics[count].global_irq_base = io_apic->GlobalIrqBase;
    count++;
    return ZX_OK;
  };
  auto status = ForEachInMadt(ACPI_MADT_TYPE_IO_APIC, visitor);
  if (status != ZX_OK) {
    return status;
  }

  *io_apics_count = count;
  return ZX_OK;
}

zx_status_t AcpiTables::interrupt_source_overrides_count(uint32_t* overrides_count) const {
  return NumInMadt(ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, overrides_count);
}

zx_status_t AcpiTables::interrupt_source_overrides(io_apic_isa_override* overrides,
                                                   uint32_t array_size,
                                                   uint32_t* overrides_count) const {
  uint32_t count = 0;
  auto visitor = [overrides, array_size, &count](ACPI_SUBTABLE_HEADER* record) {
    if (count >= array_size) {
      return ZX_ERR_INVALID_ARGS;
    }

    ACPI_MADT_INTERRUPT_OVERRIDE* iso = (ACPI_MADT_INTERRUPT_OVERRIDE*)record;

    ASSERT(iso->Bus == 0);  // 0 means ISA, ISOs are only ever for ISA IRQs
    overrides[count].isa_irq = iso->SourceIrq;
    overrides[count].remapped = true;
    overrides[count].global_irq = iso->GlobalIrq;

    uint32_t flags = iso->IntiFlags;
    uint32_t polarity = flags & ACPI_MADT_POLARITY_MASK;
    uint32_t trigger = flags & ACPI_MADT_TRIGGER_MASK;

    // Conforms below means conforms to the bus spec.  ISA is
    // edge triggered and active high.
    switch (polarity) {
      case ACPI_MADT_POLARITY_CONFORMS:
      case ACPI_MADT_POLARITY_ACTIVE_HIGH:
        overrides[count].pol = IRQ_POLARITY_ACTIVE_HIGH;
        break;
      case ACPI_MADT_POLARITY_ACTIVE_LOW:
        overrides[count].pol = IRQ_POLARITY_ACTIVE_LOW;
        break;
      default:
        panic("Unknown IRQ polarity in override: %u\n", polarity);
    }

    switch (trigger) {
      case ACPI_MADT_TRIGGER_CONFORMS:
      case ACPI_MADT_TRIGGER_EDGE:
        overrides[count].tm = IRQ_TRIGGER_MODE_EDGE;
        break;
      case ACPI_MADT_TRIGGER_LEVEL:
        overrides[count].tm = IRQ_TRIGGER_MODE_LEVEL;
        break;
      default:
        panic("Unknown IRQ trigger in override: %u\n", trigger);
    }

    count++;
    return ZX_OK;
  };

  auto status = ForEachInMadt(ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, visitor);
  if (status != ZX_OK) {
    return status;
  }

  *overrides_count = count;
  return ZX_OK;
}

zx_status_t AcpiTables::NumInMadt(uint8_t type, uint32_t* element_count) const {
  uint32_t count = 0;
  auto visitor = [&count](ACPI_SUBTABLE_HEADER* record) {
    count++;
    return ZX_OK;
  };

  auto status = ForEachInMadt(type, visitor);
  if (status != ZX_OK) {
    return status;
  }

  *element_count = count;
  return ZX_OK;
}

template <typename V>
zx_status_t AcpiTables::ForEachInMadt(uint8_t type, V visitor) const {
  uintptr_t records_start, records_end;
  zx_status_t status = GetMadtRecordLimits(&records_start, &records_end);
  if (status != ZX_OK) {
    return status;
  }

  uintptr_t addr;
  ACPI_SUBTABLE_HEADER* record_hdr;
  for (addr = records_start; addr < records_end; addr += record_hdr->Length) {
    record_hdr = (ACPI_SUBTABLE_HEADER*)addr;
    if (record_hdr->Type == type) {
      status = visitor(record_hdr);
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  if (addr != records_end) {
    TRACEF("malformed MADT\n");
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t AcpiTables::GetMadtRecordLimits(uintptr_t* start, uintptr_t* end) const {
  ACPI_TABLE_HEADER* table = nullptr;
  ACPI_STATUS status = tables_->GetTable((char*)ACPI_SIG_MADT, 1, &table);
  if (status != AE_OK) {
    TRACEF("could not find MADT\n");
    return ZX_ERR_NOT_FOUND;
  }
  ACPI_TABLE_MADT* madt = (ACPI_TABLE_MADT*)table;
  uintptr_t records_start = ((uintptr_t)madt) + sizeof(*madt);
  uintptr_t records_end = ((uintptr_t)madt) + madt->Header.Length;
  if (records_start >= records_end) {
    TRACEF("MADT wraps around address space\n");
    return ZX_ERR_INTERNAL;
  }
  // Shouldn't be too many records
  if (madt->Header.Length > 4096) {
    TRACEF("MADT suspiciously long: %u\n", madt->Header.Length);
    return ZX_ERR_INTERNAL;
  }
  *start = records_start;
  *end = records_end;
  return ZX_OK;
}

zx_status_t AcpiTables::hpet(acpi_hpet_descriptor* hpet) const {
  ACPI_TABLE_HEADER* table = NULL;
  ACPI_STATUS status = tables_->GetTable((char*)ACPI_SIG_HPET, 1, &table);
  if (status != AE_OK) {
    TRACEF("could not find HPET\n");
    return ZX_ERR_NOT_FOUND;
  }

  ACPI_TABLE_HPET* hpet_tbl = (ACPI_TABLE_HPET*)table;
  if (hpet_tbl->Header.Length != sizeof(ACPI_TABLE_HPET)) {
    TRACEF("Unexpected HPET table length\n");
    return ZX_ERR_NOT_FOUND;
  }

  hpet->minimum_tick = hpet_tbl->MinimumTick;
  hpet->sequence = hpet_tbl->Sequence;
  hpet->address = hpet_tbl->Address.Address;
  switch (hpet_tbl->Address.SpaceId) {
    case ACPI_ADR_SPACE_SYSTEM_IO:
      hpet->port_io = true;
      break;
    case ACPI_ADR_SPACE_SYSTEM_MEMORY:
      hpet->port_io = false;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t AcpiTables::debug_port(AcpiDebugPortDescriptor* desc) const {
  // Find the DBG2 table entry.
  ACPI_TABLE_HEADER* table;
  ACPI_STATUS acpi_status = tables_->GetTable((char*)ACPI_SIG_DBG2, 1, &table);
  if (acpi_status != AE_OK) {
    TRACEF("acpi: could not find debug port (v2) ACPI entry\n");
    return ZX_ERR_NOT_FOUND;
  }

  // Read the DBG2 header.
  ACPI_TABLE_DBG2 debug_table;
  fbl::Span<const uint8_t> payload;
  zx_status_t status = ReadAcpiEntry(table, &debug_table, &payload);
  if (status != ZX_OK) {
    TRACEF("acpi: Failed to read DBG2 ACPI header.\n");
    return status;
  }

  // Ensure at least one debug port.
  if (debug_table.InfoCount < 1) {
    TRACEF("acpi: DBG2 table contains no debug ports.\n");
    return ZX_ERR_NOT_FOUND;
  }

  // Read the first device payload.
  ACPI_DBG2_DEVICE device;
  fbl::Span<const uint8_t> device_payload;
  status = ReadVariableLengthStruct<ACPI_DBG2_DEVICE>(payload,
                                                      /*length_field=*/&ACPI_DBG2_DEVICE::Length,
                                                      /*out=*/&device,
                                                      /*payload=*/&device_payload,
                                                      /*offset=*/debug_table.InfoOffset);
  if (status != ZX_OK) {
    TRACEF("acpi: Could not parse DBG2 device.\n");
    return status;
  }

  // Ensure we are a supported type.
  if (device.PortType != ACPI_DBG2_SERIAL_PORT ||
      device.PortSubtype != ACPI_DBG2_16550_COMPATIBLE) {
    TRACEF("acpi: DBG2 debug port unsuported. (type=%x, subtype=%x)\n", device.PortType,
           device.PortSubtype);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // We need at least one register.
  if (device.RegisterCount < 1) {
    TRACEF("acpi: DBG2 debug port doesn't have any registers defined.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Get base address and length.
  ACPI_GENERIC_ADDRESS address;
  status = ReadStruct(device_payload, &address, /*offset=*/device.BaseAddressOffset);
  if (status != ZX_OK) {
    TRACEF("acpi: Failed to read DBG2 address registers.\n");
    return status;
  }
  uint32_t address_length;
  status = ReadStruct(device_payload, &address_length, /*offset=*/device.AddressSizeOffset);
  if (status != ZX_OK) {
    TRACEF("acpi: Failed to read DBG2 address length.\n");
    return status;
  }

  // Ensure we are a MMIO address.
  if (address.SpaceId != kAcpiAddressSpaceMemory) {
    TRACEF("acpi: Address space unsupported (space_id=%x)\n", address.SpaceId);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Return information.
  desc->address = static_cast<paddr_t>(address.Address);

  return ZX_OK;
}
