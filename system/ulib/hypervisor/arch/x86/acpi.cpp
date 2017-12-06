// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/x86/acpi.h>

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <hypervisor/address.h>
#include <sys/stat.h>

extern "C" {
#include <acpica/acpi.h>
#include <acpica/actables.h>
#include <acpica/actypes.h>
}

static uint8_t acpi_checksum(void* table, uint32_t length) {
    uint8_t sum = 0;
    uint8_t* start = reinterpret_cast<uint8_t*>(table);
    uint8_t* end = start + length;
    for (; start != end; ++start)
        sum = static_cast<uint8_t>(sum + *start);
    return static_cast<uint8_t>(UINT8_MAX - sum + 1);
}

static void acpi_header(ACPI_TABLE_HEADER* header, const char* table_id, const char* signature,
                        uint32_t length) {
    memcpy(header->Signature, signature, ACPI_NAME_SIZE);
    header->Length = length;
    memcpy(header->OemId, "ZX", 2);
    memcpy(header->OemTableId, table_id, ACPI_OEM_TABLE_ID_SIZE);
    header->OemRevision = 0;
    header->Checksum = acpi_checksum(header, header->Length);
}

static zx_status_t load_file(const char* path, uintptr_t addr, size_t size, uint32_t* actual) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open ACPI table \"%s\"\n", path);
        return ZX_ERR_IO;
    }
    struct stat stat;
    int ret = fstat(fd, &stat);
    if (ret < 0) {
        fprintf(stderr, "Failed to stat ACPI table \"%s\"\n", path);
        return ZX_ERR_IO;
    }
    if ((size_t)stat.st_size > size) {
        fprintf(stderr, "Not enough space for ACPI table \"%s\"\n", path);
        return ZX_ERR_IO;
    }
    ssize_t count = read(fd, (void*)addr, stat.st_size);
    if (count < 0 || count != stat.st_size) {
        fprintf(stderr, "Failed to read ACPI table \"%s\"\n", path);
        return ZX_ERR_IO;
    }
    *actual = static_cast<uint32_t>(stat.st_size);
    return ZX_OK;
}

static void* madt_subtable(void* base, uint32_t off, uint8_t type, uint8_t length) {
    ACPI_SUBTABLE_HEADER* subtable = (ACPI_SUBTABLE_HEADER*)((uint8_t*)base + off);
    subtable->Type = type;
    subtable->Length = length;
    return subtable;
}

static zx_status_t create_madt(uintptr_t addr, size_t size, zx_vaddr_t io_apic_addr, size_t num_cpus,
                               uint32_t* actual) {
    uint32_t table_size = static_cast<uint32_t>(sizeof(ACPI_TABLE_MADT) +
                                                (num_cpus * sizeof(ACPI_MADT_LOCAL_APIC)) +
                                                sizeof(ACPI_MADT_IO_APIC));
    if (table_size > size) {
        fprintf(stderr, "Not enough space for MADT table\n");
        return ZX_ERR_IO;
    }

    ACPI_TABLE_MADT* madt = reinterpret_cast<ACPI_TABLE_MADT*>(addr);
    acpi_header(&madt->Header, "ZX MADT", ACPI_SIG_MADT, table_size);

    uint32_t offset = sizeof(ACPI_TABLE_MADT);
    for (uint8_t id = 0; id < num_cpus; ++id) {
        ACPI_MADT_LOCAL_APIC* local_apic = reinterpret_cast<ACPI_MADT_LOCAL_APIC*>(
            madt_subtable(madt, offset, ACPI_MADT_TYPE_LOCAL_APIC, sizeof(ACPI_MADT_LOCAL_APIC)));
        local_apic->ProcessorId = id;
        local_apic->Id = id;
        local_apic->LapicFlags = ACPI_MADT_ENABLED;
        offset += static_cast<uint32_t>(sizeof(ACPI_MADT_LOCAL_APIC));
    }

    ACPI_MADT_IO_APIC* io_apic = reinterpret_cast<ACPI_MADT_IO_APIC*>(
        madt_subtable(madt, offset, ACPI_MADT_TYPE_IO_APIC, sizeof(ACPI_MADT_IO_APIC)));
    io_apic->Reserved = 0;
    io_apic->Address = static_cast<uint32_t>(io_apic_addr);
    io_apic->GlobalIrqBase = 0;

    *actual = table_size;
    return ZX_OK;
}

zx_status_t create_acpi_table(const acpi_config& cfg, uintptr_t addr, size_t size,
                              uintptr_t acpi_off) {
    if (size < acpi_off + PAGE_SIZE)
        return ZX_ERR_BUFFER_TOO_SMALL;

    const uint32_t rsdt_entries = 3;
    const uint32_t rsdt_length = sizeof(ACPI_TABLE_RSDT) + (rsdt_entries - 1) * sizeof(uint32_t);

    // RSDP. ACPI 1.0.
    ACPI_RSDP_COMMON* rsdp = (ACPI_RSDP_COMMON*)(addr + acpi_off);
    ACPI_MAKE_RSDP_SIG(rsdp->Signature);
    memcpy(rsdp->OemId, "ZX", 2);
    rsdp->RsdtPhysicalAddress = static_cast<uint32_t>(acpi_off + sizeof(ACPI_RSDP_COMMON));
    rsdp->Checksum = acpi_checksum(rsdp, ACPI_RSDP_CHECKSUM_LENGTH);

    // FADT.
    const uint32_t fadt_off = rsdp->RsdtPhysicalAddress + rsdt_length;
    ACPI_TABLE_FADT* fadt = (ACPI_TABLE_FADT*)(addr + fadt_off);
    const uint32_t dsdt_off = static_cast<uint32_t>(fadt_off + sizeof(ACPI_TABLE_FADT));
    fadt->Dsdt = dsdt_off;
    fadt->Pm1aEventBlock = PM1_EVENT_PORT;
    fadt->Pm1EventLength = (ACPI_PM1_REGISTER_WIDTH / 8) * 2 /* enable and status registers */;
    fadt->Pm1aControlBlock = PM1_CONTROL_PORT;
    fadt->Pm1ControlLength = ACPI_PM1_REGISTER_WIDTH / 8;
    // Table ID must match RSDT.
    acpi_header(&fadt->Header, "ZX ACPI", ACPI_SIG_FADT, sizeof(ACPI_TABLE_FADT));

    // DSDT.
    uint32_t actual;
    zx_status_t status = load_file(cfg.dsdt_path, addr + dsdt_off, size - dsdt_off, &actual);
    if (status != ZX_OK)
        return status;

    // MADT.
    const uint32_t madt_off = dsdt_off + actual;
    status = create_madt(addr + madt_off, size - madt_off, cfg.io_apic_addr, cfg.num_cpus, &actual);

    if (status != ZX_OK)
        return status;

    // MCFG.
    const uint32_t mcfg_off = madt_off + actual;
    status = load_file(cfg.mcfg_path, addr + mcfg_off, size - mcfg_off, &actual);
    if (status != ZX_OK)
        return status;

    // RSDT.
    ACPI_TABLE_RSDT* rsdt = (ACPI_TABLE_RSDT*)(addr + rsdp->RsdtPhysicalAddress);
    rsdt->TableOffsetEntry[0] = fadt_off;
    rsdt->TableOffsetEntry[1] = madt_off;
    rsdt->TableOffsetEntry[2] = mcfg_off;
    // Table ID must match FADT.
    acpi_header(&rsdt->Header, "ZX ACPI", ACPI_SIG_RSDT, rsdt_length);
    return ZX_OK;
}
