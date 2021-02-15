// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <platform/pc.h>
#include <platform/pc/efi.h>
#include <platform/pc/smbios.h>

namespace {

// Is the string `needle` in the string `haysack`?
bool StringContains(ktl::string_view haysack, ktl::string_view needle) {
  return haysack.find(needle) != ktl::string_view::npos;
}

// Return true if the the named platform/manufacturer is expected to have
// functioning EFI support.
//
// Return false if no EFI is expected or unknown.
//
// While most x86_64 platforms _will_ have EFI support, but some platforms (in
// particular, QEMU) don't have EFI support, and this is fine.
bool IsEfiExpected(ktl::string_view manufacturer, ktl::string_view product) {
  // All Intel NUCs are expected to have functining EFI support.
  return StringContains(manufacturer, "Intel") && StringContains(product, "NUC");
}

// Ensure EFI is present on platforms we know should have it.
//
// This test aims to prevent EFI support from being silently dropped.
__NO_ASAN bool TestEfiPresent() {
  BEGIN_TEST;

  // Attempt to fetch EFI services.
  EfiServicesActivation services = TryActivateEfiServices();

  // Ensure we got back a valid result if EFI meant to be present.
  if (IsEfiExpected(manufacturer, product)) {
    EXPECT_TRUE(services.valid());
  } else {
    printf(
        "Unknown if EFI is expected to be supported on platform "
        "(manufaturer=\"%s\", product=\"%s\"). Skipping test.\n",
        manufacturer, product);
  }

  END_TEST;
}

__NO_ASAN bool TestEfiServices() {
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

}  // namespace

UNITTEST_START_TESTCASE(efi_services_tests)
UNITTEST("test_efi_present", TestEfiPresent)
UNITTEST("test_efi_services", TestEfiServices)
UNITTEST_END_TESTCASE(efi_services_tests, "efi", "EFI service tests")
