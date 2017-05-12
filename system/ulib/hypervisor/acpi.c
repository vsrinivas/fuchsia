// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <limits.h>

#include <hypervisor/guest.h>

#if __x86_64__
#include <acpica/acpi.h>
#include <acpica/actables.h>

static const size_t kIoApicAddress = 0xfec00000;

static uint8_t acpi_checksum(void* table, uint32_t length) {
    return UINT8_MAX - AcpiTbChecksum(table, length) + 1;
}

static void acpi_header(ACPI_TABLE_HEADER* header, const char* signature, uint32_t length) {
    memcpy(header->Signature, signature, ACPI_NAME_SIZE);
    header->Length = length;
    header->Checksum = acpi_checksum(header, header->Length);
}

static void* madt_subtable(void* base, uint32_t off, uint8_t type, uint8_t length) {
    ACPI_SUBTABLE_HEADER* subtable = (ACPI_SUBTABLE_HEADER*)(base + off);
    subtable->Type = type;
    subtable->Length = length;
    return subtable;
}
#endif // __x86_64__

mx_status_t guest_create_acpi_table(uintptr_t addr, size_t size, uintptr_t acpi_off) {
#if __x86_64__
    if (size < acpi_off + PAGE_SIZE)
        return ERR_BUFFER_TOO_SMALL;

    // RSDP header. ACPI 1.0.
    ACPI_RSDP_COMMON* rsdp = (ACPI_RSDP_COMMON*)(addr + acpi_off);
    ACPI_MAKE_RSDP_SIG(rsdp->Signature);
    memcpy(rsdp->OemId, "MX_HYP", ACPI_OEM_ID_SIZE);
    rsdp->RsdtPhysicalAddress = acpi_off + sizeof(ACPI_RSDP_COMMON);
    rsdp->Checksum = acpi_checksum(rsdp, ACPI_RSDP_CHECKSUM_LENGTH);

    // RSDT header.
    ACPI_TABLE_RSDT* rsdt = (ACPI_TABLE_RSDT*)(addr + rsdp->RsdtPhysicalAddress);
    uint32_t rsdt_length = sizeof(ACPI_TABLE_RSDT)/* + num_entries - 1*/;
    rsdt->TableOffsetEntry[0] = rsdp->RsdtPhysicalAddress + rsdt_length;
    acpi_header(&rsdt->Header, ACPI_SIG_RSDT, rsdt_length);

    // MADT header.
    ACPI_TABLE_MADT* madt = (ACPI_TABLE_MADT*)(addr + rsdt->TableOffsetEntry[0]);
    uint32_t madt_length = sizeof(ACPI_TABLE_MADT) + sizeof(ACPI_MADT_IO_APIC) +
                           sizeof(ACPI_MADT_LOCAL_APIC);
    acpi_header(&madt->Header, ACPI_SIG_MADT, madt_length);

    // IO APIC header.
    ACPI_MADT_IO_APIC* io_apic = madt_subtable(madt, sizeof(ACPI_TABLE_MADT),
                                               ACPI_MADT_TYPE_IO_APIC, sizeof(ACPI_MADT_IO_APIC));
    io_apic->Address = kIoApicAddress;
    io_apic->GlobalIrqBase = 0;

    // Local APIC header.
    //
    // TODO(abdulla): Expand this to support multiple CPUs.
    ACPI_MADT_LOCAL_APIC* local_apic = madt_subtable(io_apic, sizeof(ACPI_MADT_IO_APIC),
                                                     ACPI_MADT_TYPE_LOCAL_APIC,
                                                     sizeof(ACPI_MADT_LOCAL_APIC));
    local_apic->Id = 0;
    local_apic->LapicFlags = ACPI_MADT_ENABLED;

    return NO_ERROR;
#else // __x86_64__
    return ERR_NOT_SUPPORTED;
#endif // __x86_64__
}
