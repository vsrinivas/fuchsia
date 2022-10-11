// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/instrumentation/asan.h>
#include <lib/unittest/unittest.h>

#include <efi/types.h>
#include <ktl/byte.h>
#include <platform/efi.h>

#include "efi-private.h"

namespace {

// Ensure EFI is present on platforms we know should have it.
//
// This test aims to prevent EFI support from being silently dropped.
NO_ASAN bool TestEfiPresent() {
  BEGIN_TEST;

  // Grab our current aspace.
  VmAspace* old_aspace = Thread::Current::Get()->aspace();

  // Attempt to fetch EFI services.
  EfiServicesActivation services = TryActivateEfiServices();

  // Ensure we got back a valid result if EFI meant to be present.
  if (IsEfiExpected()) {
    EXPECT_TRUE(services.valid());
    // This should switch back to the old aspace.
    services.reset();

    // Make sure it actually did.
    EXPECT_EQ(old_aspace, Thread::Current::Get()->aspace());
  } else {
    printf("Unknown if EFI is expected to be supported on platform. Skipping test.\n");
  }

  END_TEST;
}

NO_ASAN bool TestEfiServices() {
  BEGIN_TEST;

  // Fetch EFI services.
  EfiServicesActivation services = TryActivateEfiServices();
  if (!services.valid()) {
    // We may not have EFI services.
    return true;
  }

  // Ensure we can call `GetTime` and get a reasonable year (between 2000 and 2100).
  efi_time time;
  efi_status result = services->GetTime(&time, nullptr);
  EXPECT_EQ(result, static_cast<efi_status>(EFI_SUCCESS));
  EXPECT_GT(time.Year, 2000);
  EXPECT_LT(time.Year, 2100);

  END_TEST;
}

bool TestMemoryAttributeTableInvalid() {
  BEGIN_TEST;

  const uint8_t kShortData[] = {0xab, 0xab, 0xab, 0xab};
  ktl::span<const ktl::byte> bytes(reinterpret_cast<const std::byte*>(kShortData),
                                   sizeof(kShortData));
  static_assert(sizeof(kShortData) < sizeof(efi_memory_attributes_table_header),
                "test data is not small enough");

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ForEachMemoryAttributeEntrySafe(
                                     bytes, [](const efi_memory_descriptor*) { return ZX_OK; }));

  END_TEST;
}

bool TestMemoryAttributeTableTruncated() {
  BEGIN_TEST;

  const uint32_t kTruncatedData[] = {
      0x2,                            // header.version
      0x10,                           // header.number_of_entries
      sizeof(efi_memory_descriptor),  // header.descriptor_size
      0,                              // header.reserved

      // descriptor 0
      EfiRuntimeServicesCode,  // descriptor.Type
      0,                       // descriptor.Padding
      0,
      0x1000,  // descriptor.PhysicalStart (64-bit)
      0,
      0,  // descriptor.VirtualStart (64-bit)
      1,
      0,  // descriptor.NumberOfPages (64-bit)
      0,
      0,  // descriptor.Attribute (64-bit)

      // descriptor 1
      EfiRuntimeServicesData,
  };

  ktl::span<const ktl::byte> bytes(reinterpret_cast<const std::byte*>(kTruncatedData),
                                   sizeof(kTruncatedData));

  bool did_callback = false;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            ForEachMemoryAttributeEntrySafe(bytes, [&](const efi_memory_descriptor*) {
              // The first descriptor is valid so this should be called at least once.
              did_callback = true;
              return ZX_OK;
            }));

  EXPECT_TRUE(did_callback);

  END_TEST;
}

bool TestMemoryAttributeTableValid() {
  BEGIN_TEST;

  const uint32_t kTruncatedData[] = {
      0x2,                            // header.version
      0x2,                            // header.number_of_entries
      sizeof(efi_memory_descriptor),  // header.descriptor_size
      0,                              // header.reserved

      // descriptor 0
      EfiRuntimeServicesCode,  // descriptor.Type
      0,                       // descriptor.Padding
      0,
      0x1000,  // descriptor.PhysicalStart (64-bit)
      0,
      0,  // descriptor.VirtualStart (64-bit)
      1,
      0,  // descriptor.NumberOfPages (64-bit)
      0,
      0,  // descriptor.Attribute (64-bit)

      // descriptor 1
      EfiRuntimeServicesCode,  // descriptor.Type
      0,                       // descriptor.Padding
      0,
      0x1000,  // descriptor.PhysicalStart (64-bit)
      0,
      0,  // descriptor.VirtualStart (64-bit)
      1,
      0,  // descriptor.NumberOfPages (64-bit)
      0,
      0,  // descriptor.Attribute (64-bit)
  };

  ktl::span<const ktl::byte> bytes(reinterpret_cast<const std::byte*>(kTruncatedData),
                                   sizeof(kTruncatedData));

  size_t callback_count = 0;
  EXPECT_EQ(ZX_OK, ForEachMemoryAttributeEntrySafe(bytes, [&](const efi_memory_descriptor*) {
              // The first descriptor is valid so this should be called at least once.
              callback_count++;
              return ZX_OK;
            }));

  EXPECT_EQ(2u, callback_count);

  END_TEST;
}

bool TestMemoryAttributeTableShortDescriptor() {
  BEGIN_TEST;

  const uint32_t kTruncatedData[] = {
      0x2,                                // header.version
      0x10,                               // header.number_of_entries
      sizeof(efi_memory_descriptor) - 3,  // header.descriptor_size
      0,                                  // header.reserved

      // descriptor 0
      EfiRuntimeServicesCode,  // descriptor.Type
      0,                       // descriptor.Padding
      0,
      0x1000,  // descriptor.PhysicalStart (64-bit)
      0,
      0,  // descriptor.VirtualStart (64-bit)
      1,
      0,  // descriptor.NumberOfPages (64-bit)
      0,
      0,  // descriptor.Attribute (64-bit)

      // descriptor 1
      EfiRuntimeServicesData,
  };

  ktl::span<const ktl::byte> bytes(reinterpret_cast<const std::byte*>(kTruncatedData),
                                   sizeof(kTruncatedData));

  bool did_callback = false;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            ForEachMemoryAttributeEntrySafe(bytes, [&](const efi_memory_descriptor*) {
              // The first descriptor is valid so this should be called at least once.
              did_callback = true;
              return ZX_OK;
            }));

  EXPECT_FALSE(did_callback);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(efi_services_tests)
UNITTEST("test_efi_present", TestEfiPresent)
UNITTEST("test_efi_services", TestEfiServices)
UNITTEST("test_memory_attributes_table_valid", TestMemoryAttributeTableValid)
UNITTEST("test_memory_attributes_table_invalid", TestMemoryAttributeTableInvalid)
UNITTEST("test_memory_attributes_table_truncated", TestMemoryAttributeTableTruncated)
UNITTEST("test_memory_attributes_table_short_descriptor", TestMemoryAttributeTableShortDescriptor)
UNITTEST_END_TESTCASE(efi_services_tests, "efi", "EFI service tests")
