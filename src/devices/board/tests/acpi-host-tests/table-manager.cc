// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/tests/acpi-host-tests/table-manager.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fit/defer.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <vector>

#include <acpica/acpi.h>

#include "src/devices/board/tests/acpi-host-tests/table.h"

// This file manages loading ACPI tables and setting them up to be passed to ACPICA.
namespace acpi {
namespace {
std::unique_ptr<AcpiTableManager> MANAGER_INSTANCE;

class FadtFixup : public AcpiTableFixup {
  void Gather(AcpiTable* table) override {
    if (table->GetHeader()->Is(ACPI_SIG_DSDT)) {
      dsdt_addr_ = table->GetHeaderAddress();
    } else if (table->GetHeader()->Is(ACPI_SIG_FACS)) {
      facs_addr_ = table->GetHeaderAddress();
    }
  }

  void Fixup(AcpiTable* table) override {
    if (!table->GetHeader()->Is(ACPI_SIG_FADT)) {
      return;
    }

    ACPI_TABLE_FADT* fadt = table->GetTable<ACPI_TABLE_FADT>();
    fadt->Facs = 0;
    fadt->Dsdt = 0;
    fadt->XFacs = facs_addr_;
    fadt->XDsdt = dsdt_addr_;
    // Force HW-reduced mode, to limit the amount of hardware we have to emulate.
    fadt->Flags |= ACPI_FADT_HW_REDUCED;
    table->GetHeader()->checksum = 0;
    table->GetHeader()->checksum = ChecksumTable(fadt, fadt->Header.Length);
    printf("Fixed up FACS to 0x%lx and DSDT to 0x%lx\n", facs_addr_, dsdt_addr_);
  }

 private:
  uint64_t dsdt_addr_ = 0;
  uint64_t facs_addr_ = 0;
};

}  // namespace

AcpiTableManager* AcpiTableManager::LoadFromDir(const char* path) {
  DIR* d = opendir(path);
  if (d == nullptr) {
    printf("Failed to open '%s': %s", path, strerror(errno));
    return nullptr;
  }
  auto close_dir = fit::defer([d]() { closedir(d); });
  int fd = dirfd(d);

  std::vector<AcpiTable> tables;
  std::vector<uint64_t> xsdt_entries;

  // Loop over the directory and load any tables present.
  dirent* ent;
  while ((ent = readdir(d)) != nullptr) {
    size_t len = strlen(ent->d_name);
    if (len < 4) {
      continue;
    }
    // Only look at files with the suffix ".dat" (output by acpixtract) or ".aml" (output by iasl).
    char* suffix = ent->d_name + (len - 4);
    if (strcmp(suffix, ".dat") != 0 && strcmp(suffix, ".aml") != 0) {
      continue;
    }

    // Open the table.
    printf("Loading table '%s'... ", ent->d_name);
    int table_fd = openat(fd, ent->d_name, O_RDONLY);
    if (table_fd == -1) {
      printf("Open failed (%s)\n", strerror(errno));
      continue;
    }
    auto close_table = fit::defer([table_fd]() { close(table_fd); });

    // Figure out how much data we need to allocate.
    struct stat statbuf;
    int ok = fstat(table_fd, &statbuf);
    if (ok != 0) {
      printf("Stat failed (%s)\n", strerror(errno));
      close(table_fd);
      continue;
    }
    printf("[%ld bytes] ", statbuf.st_size);

    // Read the table.
    std::vector<uint8_t> data(statbuf.st_size);
    ssize_t bytes_read = read(table_fd, data.data(), data.size());
    if (bytes_read != statbuf.st_size) {
      printf("Read failed (%s)\n", strerror(errno));
      close(table_fd);
      continue;
    }

    // Store the table.
    AcpiTable table(std::move(data));

    tables.emplace_back(std::move(table));
    xsdt_entries.emplace_back(tables.back().GetHeaderAddress());
    printf("OK\n");
  }

  // Generate the XSDT table, which contains pointers to all of the other tables.
  auto xsdt = AcpiTable(AcpiXsdt().EncodeXsdt(std::move(xsdt_entries)));
  auto xsdt_ptr = xsdt.GetHeaderAddress();
  tables.emplace_back(std::move(xsdt));
  printf("Generated XSDT @ 0x%lx\n", xsdt_ptr);

  // Set up the manager, and update the generated RSDP to point at our generated XSDT.
  auto manager = std::make_unique<AcpiTableManager>(std::move(tables));
  manager->rsdp_.xsdt_address = xsdt_ptr;
  manager->rsdp_.Checksum();

  // We always want to fix up the FADT, because it contains pointers to the DSDT and FACS
  // in the original machine's physical memory.
  manager->AddFixup(std::unique_ptr<AcpiTableFixup>(new FadtFixup()));

  MANAGER_INSTANCE = std::move(manager);
  return MANAGER_INSTANCE.get();
}

AcpiTableManager* AcpiTableManager::Get() { return MANAGER_INSTANCE.get(); }

void AcpiTableManager::ApplyFixups() {
  for (auto& table : tables_) {
    for (auto& fixup : fixups_) {
      fixup->Gather(&table);
    }
  }

  for (auto& table : tables_) {
    for (auto& fixup : fixups_) {
      fixup->Fixup(&table);
    }
  }
}

}  // namespace acpi
