// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/apic.h>
#include <lib/acpi_lite/testing/test_util.h>

#include <vector>

#include <fbl/span.h>
#include <fuzzer/FuzzedDataProvider.h>

namespace acpi_lite::testing {
namespace {

void TestOneInput(FuzzedDataProvider& provider) {
  // Get the test data.
  std::vector<uint8_t> data = provider.ConsumeRemainingBytes<uint8_t>();

  // Ensure we have at least enough bytes for a valid header.
  if (data.size() < sizeof(AcpiSdtHeader)) {
    return;
  }

  // Update |length| to match the actual data length.
  auto* table = reinterpret_cast<AcpiSdtHeader*>(data.data());
  table->length = static_cast<uint32_t>(data.size());

  // Try and enumerate the entries.
  EnumerateIoApics(FakeAcpiParser({table}), [](const AcpiMadtIoApicEntry& entry) { return ZX_OK; });
}

}  // namespace
}  // namespace acpi_lite::testing

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  acpi_lite::testing::TestOneInput(provider);
  return 0;
}
