// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_TEST_UTIL_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_TEST_UTIL_H_

#include <lib/acpi_lite.h>

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

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_TEST_UTIL_H_
