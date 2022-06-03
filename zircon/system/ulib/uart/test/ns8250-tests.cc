// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/uart/mock.h>
#include <lib/uart/ns8250.h>
#include <lib/uart/uart.h>

#include <zxtest/zxtest.h>

namespace {

using SimpleTestDriver =
    uart::KernelDriver<uart::ns8250::MmioDriver, uart::mock::IoProvider, uart::Unsynchronized>;
constexpr dcfg_simple_t kTestConfig = {};

TEST(Ns8250Tests, HelloWorld) {
  SimpleTestDriver driver(kTestConfig);

  driver.io()
      .mock()
      .ExpectWrite(uint8_t{0b0000'0000}, 1)  // Init
      .ExpectWrite(uint8_t{0b1000'0000}, 3)
      .ExpectWrite(uint8_t{0b0000'0001}, 0)
      .ExpectWrite(uint8_t{0b0000'0000}, 1)
      .ExpectWrite(uint8_t{0b1110'0111}, 2)
      .ExpectWrite(uint8_t{0b0000'0011}, 3)
      .ExpectWrite(uint8_t{0b0000'0011}, 4)
      .ExpectRead(uint8_t{0b1110'0001}, 2)
      .ExpectRead(uint8_t{0b0110'0000}, 5)  // TxReady -> true
      .ExpectWrite(uint8_t{'h'}, 0)         // Write
      .ExpectWrite(uint8_t{'i'}, 0)
      .ExpectWrite(uint8_t{'\r'}, 0)
      .ExpectWrite(uint8_t{'\n'}, 0);

  driver.Init();
  EXPECT_EQ(3, driver.Write("hi\n"));
}

TEST(Ns8250Tests, Read) {
  SimpleTestDriver driver(kTestConfig);

  driver.io()
      .mock()
      .ExpectWrite(uint8_t{0b0000'0000}, 1)  // Init
      .ExpectWrite(uint8_t{0b1000'0000}, 3)
      .ExpectWrite(uint8_t{0b0000'0001}, 0)
      .ExpectWrite(uint8_t{0b0000'0000}, 1)
      .ExpectWrite(uint8_t{0b1110'0111}, 2)
      .ExpectWrite(uint8_t{0b0000'0011}, 3)
      .ExpectWrite(uint8_t{0b0000'0011}, 4)
      .ExpectRead(uint8_t{0b1110'0001}, 2)
      .ExpectRead(uint8_t{0b0110'0000}, 5)  // TxReady -> true
      .ExpectWrite(uint8_t{'?'}, 0)         // Write
      .ExpectWrite(uint8_t{'\r'}, 0)
      .ExpectWrite(uint8_t{'\n'}, 0)
      .ExpectRead(uint8_t{0b0110'0001}, 5)  // Read (data_ready)
      .ExpectRead(uint8_t{'q'}, 0)          // Read (data)
      .ExpectRead(uint8_t{0b0110'0001}, 5)  // Read (data_ready)
      .ExpectRead(uint8_t{'\r'}, 0);        // Read (data)

  driver.Init();
  EXPECT_EQ(2, driver.Write("?\n"));
  EXPECT_EQ(uint8_t{'q'}, driver.Read());
  EXPECT_EQ(uint8_t{'\r'}, driver.Read());
}

}  // namespace
