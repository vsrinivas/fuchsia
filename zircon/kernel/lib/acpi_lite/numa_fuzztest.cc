// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/numa.h>

#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

namespace acpi_lite {
namespace {

void TestOneInput(FuzzedDataProvider& provider) {
  // Get the test data.
  std::vector<uint8_t> data = provider.ConsumeRemainingBytes<uint8_t>();

  // Ensure we have at least enough bytes for a valid header.
  if (data.size() < sizeof(AcpiSratTable)) {
    return;
  }

  // Update |length| to match the actual data length.
  auto* table = reinterpret_cast<AcpiSratTable*>(data.data());
  table->header.length = static_cast<uint32_t>(data.size());

  // Try and parse the result.
  (void)EnumerateCpuNumaPairs(table, [](const AcpiNumaDomain& domain, uint32_t value) {});
}

}  // namespace
}  // namespace acpi_lite

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  acpi_lite::TestOneInput(provider);
  return 0;
}
