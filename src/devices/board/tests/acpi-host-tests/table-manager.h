// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_TESTS_ACPI_HOST_TESTS_TABLE_MANAGER_H
#define SRC_DEVICES_BOARD_TESTS_ACPI_HOST_TESTS_TABLE_MANAGER_H

#include <lib/stdcompat/span.h>
#include <zircon/compiler.h>

#include <memory>
#include <string>
#include <vector>

#include "src/devices/board/tests/acpi-host-tests/table.h"

namespace acpi {

// Represents a single table.
class AcpiTable {
 public:
  explicit AcpiTable(std::vector<uint8_t> data) : data_(std::move(data)) {}
  uint64_t GetHeaderAddress() { return reinterpret_cast<uint64_t>(data_.data()); }

  AcpiDescriptionTableHeader* GetHeader() { return GetTable<AcpiDescriptionTableHeader>(); }

  template <typename T>
  T* GetTable() {
    return reinterpret_cast<T*>(data_.data());
  }

 private:
  std::vector<uint8_t> data_;
};

// An interface used to modify ACPI tables. Gather() is called on each table present in the system,
// and then Fixup() is called on each table.
class AcpiTableFixup {
 public:
  virtual void Gather(AcpiTable* table) {}

  virtual void Fixup(AcpiTable* table) {}

  virtual ~AcpiTableFixup() = default;
};

// Manages a set of tables.
class AcpiTableManager {
 public:
  // Initialize the AcpiTableManager instance from the given directory.
  static AcpiTableManager* LoadFromDir(const char* path);
  static AcpiTableManager* LoadFromDir(std::string path) { return LoadFromDir(path.data()); }
  // Get the current AcpiTableManager instance. Will crash if one is not set.
  static AcpiTableManager* Get();
  explicit AcpiTableManager(std::vector<AcpiTable> tables) : tables_(std::move(tables)) {}

  // Fix up the tables
  void ApplyFixups();

  // Add a fixup to be performed on the table
  void AddFixup(std::unique_ptr<AcpiTableFixup> fixup) { fixups_.emplace_back(std::move(fixup)); }

  // Get the pointer to the RSDP.
  void* GetRsdp() { return &rsdp_; }

 private:
  std::vector<AcpiTable> tables_;
  std::vector<std::unique_ptr<AcpiTableFixup>> fixups_;
  AcpiRsdp rsdp_;
};

}  // namespace acpi

#endif
