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
#include "src/virtualization/bin/vmm/arch/x64/rtc_mc146818.h"

static uint8_t acpi_checksum(void* table, uint32_t length) {
  uint8_t sum = 0;
  uint8_t* start = reinterpret_cast<uint8_t*>(table);
  uint8_t* end = start + length;
  for (; start != end; ++start) {
    sum = static_cast<uint8_t>(sum + *start);
  }
  return static_cast<uint8_t>(UINT8_MAX - sum + 1);
}

static void acpi_header_no_checksum(ACPI_TABLE_HEADER* header, const char* table_id,
                                    const char* signature, uint8_t revision, uint32_t length) {
  memset(header, 0, sizeof(*header));
  memcpy(header->Signature, signature, ACPI_NAMESEG_SIZE);
  header->Revision = revision;
  header->Length = length;
  memcpy(header->OemId, "ZX", 2);
  memcpy(header->OemTableId, table_id, ACPI_OEM_TABLE_ID_SIZE);
  header->OemRevision = 0;
  header->Checksum = 0;
}

static void acpi_header(ACPI_TABLE_HEADER* header, const char* table_id, const char* signature,
                        uint8_t revision, uint32_t length) {
  acpi_header_no_checksum(header, table_id, signature, revision, length);
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
  ret = read(fd.get(), phys_mem.ptr(off, stat.st_size), stat.st_size);
  if (ret != stat.st_size) {
    FX_LOGS(ERROR) << "Failed to read ACPI table " << path;
    return ZX_ERR_IO;
  }
  *actual = static_cast<uint32_t>(stat.st_size);
  return ZX_OK;
}

static zx_status_t create_madt(const PhysMem& phys_mem, zx_vaddr_t offset, zx_vaddr_t io_apic_addr,
                               uint8_t num_cpus, uint32_t* actual) {
  uint32_t table_size =
      static_cast<uint32_t>(sizeof(ACPI_TABLE_MADT) + (num_cpus * sizeof(ACPI_MADT_LOCAL_APIC)) +
                            sizeof(ACPI_MADT_IO_APIC));

  const zx_vaddr_t madt_offset = offset;
  offset += sizeof(ACPI_TABLE_MADT);
  for (uint8_t id = 0; id < num_cpus; ++id) {
    ACPI_MADT_LOCAL_APIC local_apic;
    memset(&local_apic, 0, sizeof(local_apic));
    local_apic.Header.Type = ACPI_MADT_TYPE_LOCAL_APIC;
    local_apic.Header.Length = sizeof(ACPI_MADT_LOCAL_APIC);
    local_apic.ProcessorId = id;
    local_apic.Id = id;
    local_apic.LapicFlags = ACPI_MADT_ENABLED;
    phys_mem.write(offset, local_apic);
    offset += sizeof(ACPI_MADT_LOCAL_APIC);
  }

  ACPI_MADT_IO_APIC io_apic;
  memset(&io_apic, 0, sizeof(io_apic));
  io_apic.Header.Type = ACPI_MADT_TYPE_IO_APIC;
  io_apic.Header.Length = sizeof(ACPI_MADT_IO_APIC);
  io_apic.Reserved = 0;
  io_apic.Address = static_cast<uint32_t>(io_apic_addr);
  io_apic.GlobalIrqBase = 0;
  phys_mem.write(offset, io_apic);

  // add header without checksum
  ACPI_TABLE_MADT madt;
  memset(&madt, 0, sizeof(madt));
  acpi_header_no_checksum(&madt.Header, "ZX MADT", ACPI_SIG_MADT, 4, table_size);
  phys_mem.write(madt_offset, madt);

  // Now compute checksum over the whole range and re-write header with checksum
  madt.Header.Checksum = acpi_checksum(phys_mem.ptr(madt_offset, table_size), table_size);
  phys_mem.write(madt_offset, madt);

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
  ACPI_RSDP_COMMON rsdp;
  memset(&rsdp, 0, sizeof(rsdp));
  ACPI_MAKE_RSDP_SIG(rsdp.Signature);
  memcpy(rsdp.OemId, "ZX", 2);
  rsdp.RsdtPhysicalAddress = static_cast<uint32_t>(kAcpiOffset + sizeof(ACPI_RSDP_COMMON));
  rsdp.Checksum = 0;
  rsdp.Revision = 0;
  rsdp.Checksum = acpi_checksum(&rsdp, ACPI_RSDP_CHECKSUM_LENGTH);
  phys_mem.write(kAcpiOffset, rsdp);

  // FADT.
  const uint32_t fadt_off = rsdp.RsdtPhysicalAddress + rsdt_length;
  ACPI_TABLE_FADT fadt;
  memset(&fadt, 0, sizeof(fadt));
  const uint32_t dsdt_off = static_cast<uint32_t>(fadt_off + sizeof(ACPI_TABLE_FADT));
  fadt.Dsdt = dsdt_off;
  fadt.Pm1aEventBlock = kPm1EventPort;
  fadt.Pm1EventLength = (ACPI_PM1_REGISTER_WIDTH / 8) * 2 /* enable and status registers */;
  fadt.Pm1aControlBlock = kPm1ControlPort;
  fadt.Pm1ControlLength = ACPI_PM1_REGISTER_WIDTH / 8;
  fadt.Century = static_cast<uint8_t>(RtcMc146818::Register::kCentury);
  // Table ID must match RSDT.
  acpi_header(&fadt.Header, "ZX ACPI", ACPI_SIG_FADT, 6, sizeof(ACPI_TABLE_FADT));
  phys_mem.write(fadt_off, fadt);

  // DSDT.
  uint32_t actual;
  zx_status_t status = load_file(cfg.dsdt_path, phys_mem, dsdt_off, &actual);
  if (status != ZX_OK) {
    return status;
  }

  // MADT.
  const uint32_t madt_off = dsdt_off + actual;
  status = create_madt(phys_mem, madt_off, cfg.io_apic_addr, cfg.cpus, &actual);
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
  ACPI_TABLE_RSDT rsdt;
  memset(&rsdt, 0, sizeof(rsdt));
  // the rsdt.TableOffsetEntry field is a variable sized array, and we have a stack allocation. As
  // such we must only manipulate the rsdt.Header directly, and write our table entries directly
  // into guest memory.
  const zx_vaddr_t table_offset =
      rsdp.RsdtPhysicalAddress + offsetof(ACPI_TABLE_RSDT, TableOffsetEntry);
  assert(table_offset == rsdp.RsdtPhysicalAddress + sizeof(ACPI_TABLE_HEADER));
  phys_mem.write(table_offset + sizeof(uint32_t) * 0, fadt_off);
  phys_mem.write(table_offset + sizeof(uint32_t) * 1, madt_off);
  phys_mem.write(table_offset + sizeof(uint32_t) * 2, mcfg_off);
  // Table ID must match FADT.
  acpi_header_no_checksum(&rsdt.Header, "ZX ACPI", ACPI_SIG_RSDT, 1, rsdt_length);
  phys_mem.write(rsdp.RsdtPhysicalAddress, rsdt.Header);
  // Now generate full checksum and re-write the header
  rsdt.Header.Checksum =
      acpi_checksum(phys_mem.ptr(rsdp.RsdtPhysicalAddress, rsdt_length), rsdt_length);
  phys_mem.write(rsdp.RsdtPhysicalAddress, rsdt.Header);

  return ZX_OK;
}
