// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_tables.h>

#include <assert.h>
#include <err.h>
#include <lk/init.h>
#include <trace.h>

#define LOCAL_TRACE 0

namespace {

constexpr uint32_t kAcpiMaxInitTables = 32;
ACPI_TABLE_DESC acpi_tables[kAcpiMaxInitTables];

} // namespace

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

zx_status_t AcpiTables::interrupt_source_overrides(
    io_apic_isa_override* overrides, uint32_t array_size, uint32_t* overrides_count) const {

    uint32_t count = 0;
    auto visitor = [overrides, array_size, &count](ACPI_SUBTABLE_HEADER* record) {
        if (count >= array_size) {
            return ZX_ERR_INVALID_ARGS;
        }

        ACPI_MADT_INTERRUPT_OVERRIDE* iso = (ACPI_MADT_INTERRUPT_OVERRIDE*)record;

        ASSERT(iso->Bus == 0); // 0 means ISA, ISOs are only ever for ISA IRQs
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
            panic("Unknown IRQ polarity in override: %u\n",
                  polarity);
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
            panic("Unknown IRQ trigger in override: %u\n",
                  trigger);
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
