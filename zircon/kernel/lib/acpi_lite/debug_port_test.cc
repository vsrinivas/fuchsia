// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/debug_port.h>
#include <lib/acpi_lite/testing/test_data.h>
#include <lib/acpi_lite/testing/test_util.h>
#include <lib/zx/status.h>

#include <memory>

#include <gtest/gtest.h>

namespace acpi_lite::testing {
namespace {

TEST(DebugPort, ParseChromebookAtlas) {
  FakeAcpiParser parser({PixelbookAtlasAcpiParser()});

  zx::status<AcpiDebugPortDescriptor> debug_port = GetDebugPort(parser);
  ASSERT_TRUE(debug_port.is_ok());
  EXPECT_EQ(0xfe03'4000u, debug_port->address);
  EXPECT_EQ(0x1000u, debug_port->length);
}

}  // namespace
}  // namespace acpi_lite::testing
