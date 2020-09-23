// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/acpi.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <acpica/acpi.h>
#include <fbl/unique_fd.h>

#include "src/virtualization/bin/vmm/arch/x64/io_port.h"

static uint8_t acpi_checksum(void* table, uint32_t length) {
  uint8_t sum = 0;
  uint8_t* start = reinterpret_cast<uint8_t*>(table);
  uint8_t* end = start + length;
  for (; start != end; ++start) {
    sum = static_cast<uint8_t>(sum + *start);
  }
  return static_cast<uint8_t>(UINT8_MAX - sum + 1);
}

static void acpi_header(ACPI_TABLE_HEADER* header, const char* table_id, const char* signature,
                        uint8_t revision, uint32_t length) {
  memset(header, 0, sizeof(*header));
  memcpy(header->Signature, signature, ACPI_NAMESEG_SIZE);
  header->Revision = revision;
  header->Length = length;
  memcpy(header->OemId, "ZX", 2);
  memcpy(header->OemTableId, table_id, ACPI_OEM_TABLE_ID_SIZE);
  header->OemRevision = 0;
  header->Checksum = acpi_checksum(header, header->Length);
}

static zx_status_t load_file(const char* path, const PhysMem& phys_mem, uint32_t off,
                             uint32_t* actual) {
  fbl::unique_fd fd(open(path, O_RDONLY));
  if (!fd) {
    FX_LOGS(ERROR) << "Failed to open ACPI table " << path;
    return ZX_ERR_IO;
  }
  struct stat stat;
  ssize_t ret = fstat(fd.get(), &stat);
  if (ret < 0) {
    FX_LOGS(ERROR) << "Failed to stat ACPI table " << path;
    return ZX_ERR_IO;
  }
  ret = read(fd.get(), phys_mem.as<void>(off, stat.st_size), stat.st_size);
  if (ret != stat.st_size) {
    FX_LOGS(ERROR) << "Failed to read ACPI table " << path;
    return ZX_ERR_IO;
  }
  *actual = static_cast<uint32_t>(stat.st_size);
  return ZX_OK;
}

template <typename T>
static T* madt_subtable(void* base, uint32_t off, uint8_t type) {
  auto subtable = reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off);
  subtable->Header.Type = type;
  subtable->Header.Length = sizeof(T);
  return subtable;
}

static zx_status_t create_madt(ACPI_TABLE_MADT* madt, zx_vaddr_t io_apic_addr, uint8_t num_cpus,
                               uint32_t* actual) {
  uint32_t table_size =
      static_cast<uint32_t>(sizeof(ACPI_TABLE_MADT) + (num_cpus * sizeof(ACPI_MADT_LOCAL_APIC)) +
                            sizeof(ACPI_MADT_IO_APIC));

  uint32_t offset = sizeof(ACPI_TABLE_MADT);
  for (uint8_t id = 0; id < num_cpus; ++id) {
    auto local_apic = madt_subtable<ACPI_MADT_LOCAL_APIC>(madt, offset, ACPI_MADT_TYPE_LOCAL_APIC);
    local_apic->ProcessorId = id;
    local_apic->Id = id;
    local_apic->LapicFlags = ACPI_MADT_ENABLED;
    offset += static_cast<uint32_t>(sizeof(ACPI_MADT_LOCAL_APIC));
  }

  auto io_apic = madt_subtable<ACPI_MADT_IO_APIC>(madt, offset, ACPI_MADT_TYPE_IO_APIC);
  io_apic->Reserved = 0;
  io_apic->Address = static_cast<uint32_t>(io_apic_addr);
  io_apic->GlobalIrqBase = 0;

  // add header, computing checksum for the entire table
  acpi_header(&madt->Header, "ZX MADT", ACPI_SIG_MADT, 4, table_size);

  *actual = table_size;
  return ZX_OK;
}

zx_status_t create_acpi_table(const AcpiConfig& cfg, const PhysMem& phys_mem) {
  if (phys_mem.size() < kAcpiOffset + PAGE_SIZE) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  const uint32_t rsdt_entries = 3;
  const uint32_t rsdt_length = sizeof(ACPI_TABLE_RSDT) + (rsdt_entries - 1) * sizeof(uint32_t);

  // RSDP. ACPI 1.0.
  auto rsdp = phys_mem.as<ACPI_RSDP_COMMON>(kAcpiOffset);
  ACPI_MAKE_RSDP_SIG(rsdp->Signature);
  memcpy(rsdp->OemId, "ZX", 2);
  rsdp->RsdtPhysicalAddress = static_cast<uint32_t>(kAcpiOffset + sizeof(ACPI_RSDP_COMMON));
  rsdp->Checksum = acpi_checksum(rsdp, ACPI_RSDP_CHECKSUM_LENGTH);

  // FADT.
  const uint32_t fadt_off = rsdp->RsdtPhysicalAddress + rsdt_length;
  auto fadt = phys_mem.as<ACPI_TABLE_FADT>(fadt_off);
  const uint32_t dsdt_off = static_cast<uint32_t>(fadt_off + sizeof(ACPI_TABLE_FADT));
  fadt->Dsdt = dsdt_off;
  fadt->Pm1aEventBlock = kPm1EventPort;
  fadt->Pm1EventLength = (ACPI_PM1_REGISTER_WIDTH / 8) * 2 /* enable and status registers */;
  fadt->Pm1aControlBlock = kPm1ControlPort;
  fadt->Pm1ControlLength = ACPI_PM1_REGISTER_WIDTH / 8;
  // Table ID must match RSDT.
  acpi_header(&fadt->Header, "ZX ACPI", ACPI_SIG_FADT, 6, sizeof(ACPI_TABLE_FADT));

  // DSDT.
  uint32_t actual;
  zx_status_t status = load_file(cfg.dsdt_path, phys_mem, dsdt_off, &actual);
  if (status != ZX_OK) {
    return status;
  }

  // MADT.
  const uint32_t madt_off = dsdt_off + actual;
  status = create_madt(phys_mem.as<ACPI_TABLE_MADT>(madt_off), cfg.io_apic_addr, cfg.cpus, &actual);
  if (status != ZX_OK) {
    return status;
  }

  // MCFG.
  const uint32_t mcfg_off = madt_off + actual;
  status = load_file(cfg.mcfg_path, phys_mem, mcfg_off, &actual);
  if (status != ZX_OK) {
    return status;
  }

  // RSDT.
  auto rsdt = phys_mem.as<ACPI_TABLE_RSDT>(rsdp->RsdtPhysicalAddress);
  rsdt->TableOffsetEntry[0] = fadt_off;
  rsdt->TableOffsetEntry[1] = madt_off;
  rsdt->TableOffsetEntry[2] = mcfg_off;
  // Table ID must match FADT.
  acpi_header(&rsdt->Header, "ZX ACPI", ACPI_SIG_RSDT, 1, rsdt_length);
  return ZX_OK;
}
