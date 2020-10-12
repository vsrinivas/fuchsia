// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_TEST_UTIL_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_TEST_UTIL_H_

#include <lib/acpi_lite.h>

#include <initializer_list>

#include <fbl/vector.h>

#include "test_data.h"

namespace acpi_lite {

// Every address just translates to 0.
class NullPhysMemReader : public PhysMemReader {
 public:
  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
};

// Every address translates to a valid but empty page.
class EmptyPhysMemReader : public PhysMemReader {
 public:
  EmptyPhysMemReader() { empty_data_ = std::make_unique<uint8_t[]>(ZX_PAGE_SIZE); }

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    if (length >= ZX_PAGE_SIZE) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    return zx::success(empty_data_.get());
  }

 private:
  std::unique_ptr<uint8_t[]> empty_data_;
};

// Emulate access of tables specified in an AcpiTableSet.
class FakePhysMemReader : public PhysMemReader {
 public:
  explicit FakePhysMemReader(const AcpiTableSet* tables) : tables_(tables) {}

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    for (const auto& table : tables_->tables) {
      if (table.phys_addr == phys && length <= table.data.size_bytes()) {
        return zx::success(table.data.data());
      }
    }
    return zx::error(ZX_ERR_NOT_FOUND);
  }

 private:
  const AcpiTableSet* tables_;
};

// An empty AcpiParser.
class EmptyAcpiParser final : public AcpiParserInterface {
  size_t num_tables() const override { return 0u; }

  const AcpiSdtHeader* GetTableAtIndex(size_t index) const override { return nullptr; }
};

// An AcpiParserInterface that provides a fixed set of tables.
//
// Input pointers must have valid |length| parameters. That is, each pointer |p|
// must point to at least |p->length| bytes of memory.
class FakeAcpiParser : public AcpiParserInterface {
 public:
  FakeAcpiParser(std::initializer_list<fbl::Span<const uint8_t>> tables) {
    for (fbl::Span<const uint8_t> table : tables) {
      ZX_ASSERT(table.size() >= sizeof(AcpiSdtHeader));
      Add(reinterpret_cast<const AcpiSdtHeader*>(table.data()));
    }
  }

  FakeAcpiParser(std::initializer_list<const AcpiSdtHeader*> tables) {
    for (const AcpiSdtHeader* table : tables) {
      Add(table);
    }
  }

  void Add(const AcpiSdtHeader* table) {
    ZX_ASSERT(table->length >= sizeof(AcpiSdtHeader));
    fbl::AllocChecker ac;
    tables_.push_back(table, &ac);
    ZX_ASSERT(ac.check());
  }

  size_t num_tables() const override { return tables_.size(); }

  const AcpiSdtHeader* GetTableAtIndex(size_t index) const override {
    if (index >= tables_.size()) {
      return nullptr;
    }
    return tables_[index];
  }

 private:
  fbl::Vector<const AcpiSdtHeader*> tables_;
};

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_TEST_UTIL_H_
