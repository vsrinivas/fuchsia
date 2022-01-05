// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/i8250.h"

#include <zircon/types.h>

#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "src/virtualization/bin/vmm/arch/x64/i8250_registers.h"
#include "src/virtualization/bin/vmm/guest.h"

namespace {

using Data = std::vector<uint8_t>;

std::pair<zx_status_t, Data> ReadSocket(zx::socket& socket) {
  Data buf;
  buf.resize(16);
  size_t actual;
  zx_status_t res = socket.read(0, &buf[0], buf.size(), &actual);
  buf.resize(actual);
  return std::make_pair(res, buf);
}

std::pair<zx_status_t, uint64_t> ReadInterruptId(I8250& uart) {
  auto iid = IoValue::FromU8(-1);
  zx_status_t res = uart.Read(I8250Register::INTERRUPT_ID, &iid);
  return std::make_pair(res, iid.u8);
}

TEST(I8250Test, Write) {
  Guest guest;
  I8250 uart;
  zx::socket send, recv;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &send, &recv));

  int interrupts = 0;
  uart.Init(
      &guest, &send, kI8250Base0, [&](int irq) { interrupts++; }, kI8250Irq0);

  // Write without interrupts.

  ASSERT_EQ(ZX_OK, uart.Write(I8250Register::TRANSMIT, IoValue::FromU8('A')));
  ASSERT_EQ(ZX_OK, uart.Write(I8250Register::TRANSMIT, IoValue::FromU8('\r')));

  EXPECT_EQ(interrupts, 0);
  EXPECT_EQ(std::make_pair(ZX_OK, Data{'A', '\r'}), ReadSocket(recv));

  // Enable "THR empty" interrupt and write again.

  ASSERT_EQ(ZX_OK, uart.Write(I8250Register::INTERRUPT_ENABLE,
                              IoValue::FromU8(kI8250InterruptIdTransmitEmpty)));
  ASSERT_EQ(ZX_OK, uart.Write(I8250Register::TRANSMIT, IoValue::FromU8('B')));

  EXPECT_EQ(interrupts, 1);
  EXPECT_EQ(std::make_pair(ZX_OK, kI8250InterruptIdTransmitEmpty), ReadInterruptId(uart));
  // Reading has a side effect of resetting the interrupt ID register, so read twice.
  EXPECT_EQ(std::make_pair(ZX_OK, kI8250InterruptIdNoInterrupt), ReadInterruptId(uart));

  // Write again, interrupt is raised again.

  ASSERT_EQ(ZX_OK, uart.Write(I8250Register::TRANSMIT, IoValue::FromU8('\r')));

  EXPECT_EQ(std::make_pair(ZX_OK, kI8250InterruptIdTransmitEmpty), ReadInterruptId(uart));
  EXPECT_EQ(interrupts, 2);
  EXPECT_EQ(std::make_pair(ZX_OK, Data{'B', '\r'}), ReadSocket(recv));

  // Disable "THR empty" interrupt and write again.

  ASSERT_EQ(ZX_OK, uart.Write(I8250Register::INTERRUPT_ENABLE, IoValue::FromU8(0)));
  ASSERT_EQ(ZX_OK, uart.Write(I8250Register::TRANSMIT, IoValue::FromU8('C')));
  ASSERT_EQ(ZX_OK, uart.Write(I8250Register::TRANSMIT, IoValue::FromU8('\r')));

  EXPECT_EQ(interrupts, 2);
  EXPECT_EQ(std::make_pair(ZX_OK, Data{'C', '\r'}), ReadSocket(recv));
}

}  // namespace
