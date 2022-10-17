// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

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

  zx::result<AcpiDebugPortDescriptor> debug_port = GetDebugPort(parser);
  ASSERT_TRUE(debug_port.is_ok());
  EXPECT_EQ(AcpiDebugPortDescriptor::Type::kMmio, debug_port->type);
  EXPECT_EQ(0xfe03'4000u, debug_port->address);
  EXPECT_EQ(0x1000u, debug_port->length);
}

TEST(DebugPort, ParseIntelNuc) {
  FakePhysMemReader reader = IntelNuc7i5dnPhysMemReader();
  AcpiParser parser = AcpiParser::Init(reader, reader.rsdp()).value();

  zx::result<AcpiDebugPortDescriptor> debug_port = GetDebugPort(parser);
  ASSERT_TRUE(debug_port.is_ok());
  EXPECT_EQ(AcpiDebugPortDescriptor::Type::kPio, debug_port->type);
  EXPECT_EQ(0x3f8u, debug_port->address);
  EXPECT_EQ(12u, debug_port->length);
}

}  // namespace
}  // namespace acpi_lite::testing
