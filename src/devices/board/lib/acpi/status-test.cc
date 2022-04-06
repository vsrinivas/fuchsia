// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/status.h"

#include <acpica/acpi.h>
#include <zxtest/zxtest.h>

namespace {

TEST(AcpiStatus, StatusNoValue) {
  acpi::status<> ret = acpi::ok();
  ASSERT_EQ(ret.status_value(), AE_OK);
  ASSERT_FALSE(ret.is_error());
  ASSERT_TRUE(ret.is_ok());
}

TEST(AcpiStatus, Error) {
  acpi::status<> ret = acpi::error(AE_NO_ACPI_TABLES);
  ASSERT_EQ(ret.status_value(), AE_NO_ACPI_TABLES);
  ASSERT_TRUE(ret.is_error());
  ASSERT_FALSE(ret.is_ok());
}

TEST(AcpiStatus, StatusWithValue) {
  acpi::status<uint32_t> ret = acpi::ok(10);
  ASSERT_EQ(ret.status_value(), AE_OK);
  ASSERT_FALSE(ret.is_error());
  ASSERT_TRUE(ret.is_ok());
  ASSERT_EQ(ret.value(), 10);
}
}  // namespace
