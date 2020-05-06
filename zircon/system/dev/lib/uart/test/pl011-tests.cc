// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/uart/mock.h>
#include <lib/uart/pl011.h>
#include <lib/uart/uart.h>

#include <zxtest/zxtest.h>

namespace {

using SimpleTestDriver =
    uart::KernelDriver<uart::pl011::Driver, uart::mock::IoProvider, uart::Unsynchronized>;
constexpr dcfg_simple_t kTestConfig = {};

TEST(Pl011Tests, HelloWorld) {
  SimpleTestDriver driver(kTestConfig);

  driver.io()
      .mock()
      .ExpectWrite(uint16_t{0b0001'0000'0001}, 0x30)  // Init
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // TxReady -> true
      .ExpectWrite(uint16_t{'h'}, 0)                  // Write
      .ExpectRead(uint16_t{0b0000'0000}, 0x18)        // TxReady -> false
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // TxReady -> true
      .ExpectWrite(uint16_t{'i'}, 0)                  // Write
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // TxReady -> true
      .ExpectWrite(uint16_t{'\r'}, 0)                 // Write
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // TxReady -> true
      .ExpectWrite(uint16_t{'\n'}, 0);                // Write

  driver.Init();
  EXPECT_EQ(3, driver.Write("hi\n"));
}

TEST(Pl011Tests, Read) {
  SimpleTestDriver driver(kTestConfig);

  driver.io()
      .mock()
      .ExpectWrite(uint16_t{0b0001'0000'0001}, 0x30)  // Init
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // TxReady -> true
      .ExpectWrite(uint16_t{'?'}, 0)                  // Write
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // TxReady -> true
      .ExpectWrite(uint16_t{'\r'}, 0)                 // Write
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // TxReady -> true
      .ExpectWrite(uint16_t{'\n'}, 0)                 // Write
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // Read (rx_fifo_empty)
      .ExpectRead(uint16_t{'q'}, 0)                   // Read (data)
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // Read (rx_fifo_empty)
      .ExpectRead(uint16_t{'\r'}, 0);                 // Read (data)

  driver.Init();
  EXPECT_EQ(2, driver.Write("?\n"));
  EXPECT_EQ(uint8_t{'q'}, driver.Read());
  EXPECT_EQ(uint8_t{'\r'}, driver.Read());
}

#if 0  // TODO(mcgrathr): later, needs more mock support to test interrupt case
TEST(Pl011Tests, Blocking) {
  SimpleTestDriver driver(kTestConfig);

  bool tx_called = false;
  bool rx_called = false;
  auto synthesize_interrupt = [&]() {
    driver.uart().Interrupt(
        driver.io(), [&tx_called]() { tx_called = true; },
        [](auto&&, auto&&) { rx_called = true; });
  };

  driver.io()
      .mock()
      .ExpectWrite(uint16_t{0b0001'0000'0001}, 0x30)  // Init
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)        // TxReady -> true
      .ExpectWrite(uint16_t{'h'}, 0)                  // Write
      .ExpectRead(uint16_t{0b0000'0000}, 0x18)        // TxReady -> false
      // EnableTxInterrupt(true)
      .ExpectRead(uint16_t{0}, 0x38)
      .ExpectWrite(uint16_t{0b0010'0000}, 0x38)
      .Then(synthesize_interrupt)
      // EnableTxInterrupt(false) from Interrupt
      .ExpectRead(uint16_t{0b0010'0000}, 0x38)
      .ExpectWrite(uint16_t{0}, 0x38)
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)  // TxReady -> true
      .ExpectWrite(uint16_t{'i'}, 0)            // Write
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)  // TxReady -> true
      .ExpectWrite(uint16_t{'\r'}, 0)           // Write
      .ExpectRead(uint16_t{0b1000'0000}, 0x18)  // TxReady -> true
      .ExpectWrite(uint16_t{'\n'}, 0);          // Write

  driver.Init();
  EXPECT_EQ(driver.Write("hi\n"), 3);
  EXPECT_TRUE(tx_called);
  EXPECT_FALSE(rx_called);
}
#endif

}  // namespace
