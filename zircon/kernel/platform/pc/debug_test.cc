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

  zbi_uart_t result;

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("none", &result));
  EXPECT_EQ(result.type, static_cast<uint32_t>(ZBI_UART_NONE));

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("legacy", &result));
  EXPECT_EQ(result.type, static_cast<uint32_t>(ZBI_UART_PC_PORT));
  EXPECT_EQ(result.base, 0x3f8u);
  EXPECT_EQ(result.irq, 4u);

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("mmio,0x12345678,1", &result));
  EXPECT_EQ(result.type, static_cast<uint32_t>(ZBI_UART_PC_MMIO));
  EXPECT_EQ(result.base, 0x12345678u);
  EXPECT_EQ(result.irq, 1u);

  EXPECT_EQ(ZX_OK, parse_serial_cmdline("ioport,0x123,2", &result));
  EXPECT_EQ(result.type, static_cast<uint32_t>(ZBI_UART_PC_PORT));
  EXPECT_EQ(result.base, 0x123u);
  EXPECT_EQ(result.irq, 2u);

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
