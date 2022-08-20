// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/uart/mock.h>
#include <lib/uart/motmot.h>
#include <lib/uart/uart.h>

#include <zxtest/zxtest.h>

namespace {

using SimpleTestDriver =
    uart::KernelDriver<uart::motmot::Driver, uart::mock::IoProvider, uart::Unsynchronized>;
constexpr zbi_dcfg_simple_t kTestConfig = {};

template <typename Mock>
void AppendInitSequence(Mock& mock) {
  mock
      // Init() sequence
      .ExpectRead(uint32_t{0b0000'0001'0000'0000'0000'0001'0000'0000}, 0xdc)   // Read FIFO Depth
      .ExpectWrite(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'1111}, 0x38)  // Write to UINTM
      .ExpectWrite(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x8)   // Write to UFCON
      // The following is a FIFO reset. Writes two bits and waits for them
      // to clear.
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x8)   // Reading back
      .ExpectWrite(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0110}, 0x8)  // Resetting FIFOs
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0110}, 0x8)   // Reading back
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0110}, 0x8)   // Reading back
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0110}, 0x8)   // Reading back
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x8)   // Reading back

      // Setting FIFO enable (bit 0)
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x8)
      .ExpectWrite(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0001}, 0x8)

      // Enabling TX/RX
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x4)  // UCON
      .ExpectWrite(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0101}, 0x4)
      // End of Init()
      ;
}

TEST(MotmotTests, HelloWorld) {
  SimpleTestDriver driver(kTestConfig);

  AppendInitSequence(driver.io().mock());
  driver.io()
      .mock()
      // Start of Write()
      .ExpectRead(uint32_t{0b0000'0001'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT (fifo full)
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT
      .ExpectWrite(uint32_t{'h'}, 0x20)                                       // UTXH
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT
      .ExpectWrite(uint32_t{'i'}, 0x20)                                       // UTXH
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT
      .ExpectWrite(uint32_t{'\r'}, 0x20)                                      // UTXH
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT
      .ExpectWrite(uint32_t{'\n'}, 0x20)                                      // UTXH
      ;
  // End of Write()

  driver.Init();
  EXPECT_EQ(3, driver.Write("hi\n"));
}

TEST(MotmotTests, Read) {
  SimpleTestDriver driver(kTestConfig);

  AppendInitSequence(driver.io().mock());
  driver.io()
      .mock()
      // Start of Write()
      .ExpectRead(uint32_t{0b0000'0001'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT (fifo full)
      .ExpectRead(uint32_t{0b0000'0001'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT (fifo full)
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT
      .ExpectWrite(uint32_t{'?'}, 0x20)                                       // UTXH
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT
      .ExpectWrite(uint32_t{'\r'}, 0x20)                                      // UTXH
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT
      .ExpectWrite(uint32_t{'\n'}, 0x20)                                      // UTXH
      // Start of Read() with nothing available
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0000}, 0x18)  // UFSTAT (fifo empty)
      // Start of Read() with 2 bytes available
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0010}, 0x18)  // UFSTAT (2 bytes)
      .ExpectRead(uint32_t{'q'}, 0x24)                                        // URXH
      .ExpectRead(uint32_t{0b0000'0000'0000'0000'0000'0000'0000'0001}, 0x18)  // UFSTAT (1 byte)
      .ExpectRead(uint32_t{'\r'}, 0x24)                                       // URXH
      ;

  driver.Init();
  EXPECT_EQ(2, driver.Write("?\n"));
  EXPECT_EQ(std::optional<uint32_t>{}, driver.Read());
  EXPECT_EQ(uint32_t{'q'}, driver.Read());
  EXPECT_EQ(uint32_t{'\r'}, driver.Read());
}

}  // namespace
