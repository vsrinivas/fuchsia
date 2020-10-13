// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Sample ACPI table dumps for different devices.

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_TESTING_TEST_DATA_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_TESTING_TEST_DATA_H_

#include <lib/acpi_lite/testing/test_util.h>

namespace acpi_lite::testing {

// Fake AcpiParser instances for different real-life hardware.
FakeAcpiParser PixelbookEveAcpiParser();
FakeAcpiParser PixelbookAtlasAcpiParser();
FakeAcpiParser Z840AcpiParser();
FakeAcpiParser Sys2970wxAcpiParser();

// Fake PhysMemReader instances for different real-life hardware.
FakePhysMemReader QemuPhysMemReader();
FakePhysMemReader FuchsiaHypervisorPhysMemReader();
FakePhysMemReader IntelNuc7i5dnPhysMemReader();

}  // namespace acpi_lite::testing

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_TESTING_TEST_DATA_H_
