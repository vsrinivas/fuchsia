// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/uart/imx.h>
#include <lib/uart/mock.h>
#include <lib/uart/uart.h>

#include <zxtest/zxtest.h>

namespace {

using SimpleTestDriver =
    uart::KernelDriver<uart::imx::Driver, uart::mock::IoProvider, uart::Unsynchronized>;
constexpr zbi_dcfg_simple_t kTestConfig = {};

TEST(ImxTests, Write) {
  SimpleTestDriver driver(kTestConfig);

  driver.io()
      .mock()
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x84)   // Initial settings
      .ExpectWrite(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0110}, 0x84)  // Init Enable Rx/Tx
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0010'0000'0000'0000}, 0x94)   // TxReady -> 1
      .ExpectWrite(uint32_t{'h'}, 0x40)                                        // Write
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0010'0000'0000'0000}, 0x94)   // TxReady -> 1
      .ExpectWrite(uint32_t{'i'}, 0x40)                                        // Write
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0010'0000'0000'0000}, 0x94)   // TxReady -> 1
      .ExpectWrite(uint32_t{'\r'}, 0x40)                                       // Write
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0010'0000'0000'0000}, 0x94)   // TxReady -> 1
      .ExpectWrite(uint32_t{'\n'}, 0x40)                                       // Write
      ;
  driver.Init();
  EXPECT_EQ(3, driver.Write("hi\n"));
}

TEST(ImxTests, Read) {
  SimpleTestDriver driver(kTestConfig);

  driver.io()
      .mock()
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x84)   // Initial settings
      .ExpectWrite(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0110}, 0x84)  // Init Enable Rx/Tx
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0001}, 0x98)   // rx ready
      .ExpectRead(uint32_t{'q'}, 0x0)                                          // Read (data)
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0001}, 0x98)   // rx ready
      .ExpectRead(uint32_t{'\r'}, 0x0);                                        // Read (data)
  ;

  driver.Init();
  EXPECT_EQ(uint32_t{'q'}, driver.Read());
  EXPECT_EQ(uint32_t{'\r'}, driver.Read());
}

}  // namespace
