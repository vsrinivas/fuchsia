// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/uart/all.h>
#include <lib/uart/mock.h>
#include <lib/uart/null.h>
#include <lib/uart/uart.h>

#include <string_view>

#include <zxtest/zxtest.h>

using namespace std::literals;

namespace {

TEST(UartTests, Nonblocking) {
  uart::KernelDriver<uart::mock::Driver, uart::mock::IoProvider, uart::mock::Sync> driver;

  driver.uart()
      .ExpectLock()
      .ExpectInit()
      .ExpectUnlock()
      // First Write call -> sends all chars, no waiting.
      .ExpectLock()
      .ExpectTxReady(true)
      .ExpectWrite("hi!"sv)
      .ExpectUnlock()
      // Second Write call -> sends half, then waits.
      .ExpectLock()
      .ExpectTxReady(true)
      .ExpectWrite("hello "sv)
      .ExpectTxReady(false)
      .ExpectWait(false)
      .ExpectTxReady(true)
      .ExpectWrite("world\r\n"sv)
      .ExpectUnlock();

  driver.Init();
  EXPECT_EQ(driver.Write("hi!"), 3);
  EXPECT_EQ(driver.Write("hello world\n"), 12);
}

TEST(UartTests, Blocking) {
  uart::KernelDriver<uart::mock::Driver, uart::mock::IoProvider, uart::mock::Sync> driver;

  driver.uart()
      .ExpectLock()
      .ExpectInit()
      .ExpectUnlock()
      // First Write call -> sends all chars, no waiting.
      .ExpectLock()
      .ExpectTxReady(true)
      .ExpectWrite("hi!"sv)
      .ExpectUnlock()
      // Second Write call -> sends half, then waits.
      .ExpectLock()
      .ExpectTxReady(true)
      .ExpectWrite("hello "sv)
      .ExpectTxReady(false)
      .ExpectWait(true)
      .ExpectEnableTxInterrupt()
      .ExpectTxReady(true)
      .ExpectWrite("world\r\n"sv)
      .ExpectUnlock();

  driver.Init();
  EXPECT_EQ(driver.Write("hi!"), 3);
  EXPECT_EQ(driver.Write("hello world\n"), 12);
}

TEST(UartTests, Null) {
  uart::KernelDriver<uart::null::Driver, uart::mock::IoProvider, uart::Unsynchronized> driver;

  driver.Init();
  EXPECT_EQ(driver.Write("hi!"), 3);
  EXPECT_EQ(driver.Write("hello world\n"), 12);
  EXPECT_FALSE(driver.Read());
}

TEST(UartTests, All) {
  using AllDriver = uart::all::KernelDriver<uart::mock::IoProvider, uart::Unsynchronized>;

  AllDriver driver;

  // Match against ZBI items to instantiate.
  EXPECT_FALSE(driver.Match({}, nullptr));

  // Use selected driver.
  driver.Visit([](auto&& driver) {
    driver.Init();
    EXPECT_EQ(driver.Write("hi!"), 3);
  });

  // Transfer state to a new instantiation and pick up using it.
  AllDriver newdriver{driver.uart()};
  newdriver.Visit([](auto&& driver) {
    EXPECT_EQ(driver.Write("hello world\n"), 12);
    EXPECT_FALSE(driver.Read());
  });
}

}  // namespace
