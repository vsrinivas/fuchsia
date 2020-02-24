// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "debug.h"

#include <assert.h>
#include <debug.h>
#include <lib/unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace {

bool test_parse_kernel_serial_arg() {
  BEGIN_TEST;

  SerialConfig result;

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("none", &result));
  EXPECT_EQ(result.type, SerialConfig::Type::kDisabled);

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("acpi", &result));
  EXPECT_EQ(result.type, SerialConfig::Type::kAcpi);

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("legacy", &result));
  EXPECT_EQ(result.type, SerialConfig::Type::kIoPort);
  EXPECT_EQ(result.config.io_port.port, 0x3f8u);
  EXPECT_EQ(result.config.io_port.irq, 4u);

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("mmio,0x12345678,1", &result));
  EXPECT_EQ(result.type, SerialConfig::Type::kMmio);
  EXPECT_EQ(result.config.mmio.phys_addr, 0x12345678u);
  EXPECT_EQ(result.config.mmio.irq, 1u);

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("ioport,0x123,2", &result));
  EXPECT_EQ(result.type, SerialConfig::Type::kIoPort);
  EXPECT_EQ(result.config.io_port.port, 0x123u);
  EXPECT_EQ(result.config.io_port.irq, 2u);

  // IRQs above 16 not supported.
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, parse_serial_cmdline("ioport,0x123,17", &result));

  // Invalid inputs.
  EXPECT_NE(ZX_OK, parse_serial_cmdline("invalid", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,,1", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,1", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,1,", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,1111111111111111111111111111111111,1", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,1,1111111111111111111111111111111111", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,string,1", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,1,string", &result));
  EXPECT_NE(ZX_OK, parse_serial_cmdline("ioport,1,1,", &result));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(pc_debug)
UNITTEST("parse_kernel_serial_arg", test_parse_kernel_serial_arg)
UNITTEST_END_TESTCASE(pc_debug, "pc_debug", "pc debug tests")
