// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/uart/amlogic.h>
#include <lib/uart/mock.h>
#include <lib/uart/uart.h>

#include <zxtest/zxtest.h>

namespace {

using SimpleTestDriver =
    uart::KernelDriver<uart::amlogic::Driver, uart::mock::IoProvider, uart::Unsynchronized>;
constexpr dcfg_simple_t kTestConfig = {};

TEST(AmlogicTests, HelloWorld) {
  SimpleTestDriver driver(kTestConfig);

  driver.io()
      .mock()
      .ExpectRead(uint32_t{0b1001'0000'0000'0000'1000'0000'0000'0000}, 0x8)  // Initial settings.
      // Non-interrupt settings are preserved on Init().
      .ExpectWrite(uint32_t{0b1000'0001'1100'0000'1011'0000'0000'0000}, 0x8)  // Init
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0011'1111'0000'0000}, 0xc)  // TxReady -> 1
      .ExpectWrite(uint32_t{'h'}, 0x0)                                       // Write
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0100'0000'0000'0000}, 0xc)  // TxReady -> 0
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0011'1111'0000'0000}, 0xc)  // TxReady -> 1
      .ExpectWrite(uint32_t{'i'}, 0x0)                                       // Write
      // There is room to transmit two characters now.
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0011'1110'0000'0000}, 0xc)  // TxReady -> 2
      .ExpectWrite(uint32_t{'\r'}, 0x0)                                      // Write
      .ExpectWrite(uint32_t{'\n'}, 0x0);                                     // Write

  driver.Init();
  EXPECT_EQ(3, driver.Write("hi\n"));
}

TEST(AmlogicTests, Read) {
  SimpleTestDriver driver(kTestConfig);

  driver.io()
      .mock()
      .ExpectRead(uint32_t{0b1001'0000'0000'0000'1000'0000'0000'0000}, 0x8)  // Initial settings.
      // Non-interrupt settings are preserved on Init().
      .ExpectWrite(uint32_t{0b1000'0001'1100'0000'1011'0000'0000'0000}, 0x8)  // Init
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0011'1101'0000'0000}, 0xc)  // TxReady -> 3
      .ExpectWrite(uint32_t{'?'}, 0x0)                                       // Write
      .ExpectWrite(uint32_t{'\r'}, 0x0)                                      // Write
      .ExpectWrite(uint32_t{'\n'}, 0x0)                                      // Write
      .ExpectRead(uint32_t{0b0000'0000'0001'0000'0000'0000'0000'0000}, 0xc)  // Read (rx_fifo_empty)
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000},
                  0xc)                 // Read (!rx_fifo_empty)
      .ExpectRead(uint32_t{'q'}, 0x4)  // Read (data)
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000},
                  0xc)                   // Read (!rx_fifo_empty)
      .ExpectRead(uint32_t{'\r'}, 0x4);  // Read (data)

  driver.Init();
  EXPECT_EQ(2, driver.Write("?\n"));
  EXPECT_EQ(std::optional<uint32_t>{}, driver.Read());
  EXPECT_EQ(uint32_t{'q'}, driver.Read());
  EXPECT_EQ(uint32_t{'\r'}, driver.Read());
}

}  // namespace
