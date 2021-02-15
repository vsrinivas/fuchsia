// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <platform/pc/efi.h>

namespace {

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
UNITTEST("test_efi_services", TestEfiServices)
UNITTEST_END_TESTCASE(efi_services_tests, "efi", "EFI service tests")
