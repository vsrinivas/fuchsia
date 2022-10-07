// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/instrumentation/asan.h>
#include <lib/unittest/unittest.h>

#include <platform/efi.h>

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

}  // namespace

UNITTEST_START_TESTCASE(efi_services_tests)
UNITTEST("test_efi_present", TestEfiPresent)
UNITTEST("test_efi_services", TestEfiServices)
UNITTEST_END_TESTCASE(efi_services_tests, "efi", "EFI service tests")
