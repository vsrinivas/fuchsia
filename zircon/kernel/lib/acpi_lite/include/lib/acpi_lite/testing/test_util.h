// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_TESTING_TEST_UTIL_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_TESTING_TEST_UTIL_H_

#include <lib/acpi_lite.h>

#include <initializer_list>

#include <fbl/span.h>
#include <fbl/vector.h>

namespace acpi_lite::testing {

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
  // A region of physical memory, starting at the given address.
  struct Region {
    zx_paddr_t phys_addr;
    fbl::Span<const uint8_t> data;
  };

  // Create a FakePhysMemReader.
  //
  // |rspd| is the physical address of the RSDP as provided by the bootloader, or 0 if
  // auto-discovery should take place on the platform.
  //
  // |tables| contains a list of tables to make available to the reader.
  explicit FakePhysMemReader(zx_paddr_t rsdp, fbl::Span<const Region> regions) : rsdp_(rsdp) {
    for (const auto& region : regions) {
      fbl::AllocChecker ac;
      regions_.push_back(region, &ac);
      ZX_ASSERT(ac.check());
    }
  }

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    for (const auto& region : regions_) {
      if (region.phys_addr == phys && length <= region.data.size_bytes()) {
        return zx::success(region.data.data());
      }
    }
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  zx_paddr_t rsdp() const { return rsdp_; }

  const fbl::Vector<Region>& regions() const { return regions_; }

 private:
  zx_paddr_t rsdp_;
  fbl::Vector<Region> regions_;
};

// An AcpiParserInterface that provides a fixed set of tables.
//
// Input pointers must have valid |length| parameters. That is, each pointer |p|
// must point to at least |p->length| bytes of memory.
class FakeAcpiParser : public AcpiParserInterface {
 public:
  FakeAcpiParser() = default;

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

}  // namespace acpi_lite::testing

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_TESTING_TEST_UTIL_H_
