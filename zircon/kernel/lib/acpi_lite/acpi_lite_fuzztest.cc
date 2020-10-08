// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_lite.h>

#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

namespace acpi_lite {
namespace {

// Emulate access of tables specified in an AcpiTableSet.
class FuzzedPhysMemReader : public PhysMemReader {
 public:
  FuzzedPhysMemReader(uint64_t addr, std::vector<uint8_t> data)
      : addr_(addr), data_(std::move(data)) {
    addr_ = std::min(addr_, UINT64_MAX - data.size());
  }

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    // PhysToPtr is responsible for ensuring no overflow.
    uint64_t phys_end;
    if (length == 0 || add_overflow(phys, length - 1, &phys_end)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }

    // Otherwise, try using looking up the regions.
    if (addr_ <= phys && phys_end < addr_ + data_.size()) {
      return zx::success(&data_[phys - addr_]);
    }

    return zx::error(ZX_ERR_NOT_FOUND);
  }

 private:
  uint64_t addr_;
  std::vector<uint8_t> data_;
};

void TestOneInput(FuzzedDataProvider& provider) {
  // Get entry point and where the test ACPI block should be mapped.
  //
  // Note that |FuzzedDataProvider| pulls these bytes off the _end_ of the input data
  // block, meaning the file format is:
  //
  //   <data to map> <8 bytes LE : map location> <8 bytes LE: entry point>
  uint64_t paddr = provider.ConsumeIntegral<uint64_t>();
  uint64_t region = provider.ConsumeIntegral<uint64_t>();

  // Create regions of memory.
  FuzzedPhysMemReader reader{region, provider.ConsumeRemainingBytes<uint8_t>()};

  zx::status<AcpiParser> parser = acpi_lite::AcpiParser::Init(reader, paddr);
  if (parser.is_ok()) {
    parser.value().GetTableBySignature(AcpiSignature("APIC"));
  }
}

}  // namespace
}  // namespace acpi_lite

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  acpi_lite::TestOneInput(provider);
  return 0;
}
