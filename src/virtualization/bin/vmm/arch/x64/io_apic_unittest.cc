// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/io_apic.h"

#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include <gtest/gtest.h>

#include "src/virtualization/bin/vmm/arch/x64/io_apic_registers.h"
#include "src/virtualization/bin/vmm/guest.h"

namespace {

// Read the given indirect register from the IO APIC.
zx_status_t ReadRegister(IoApic& io_apic, uint8_t reg, IoValue* result) {
  zx_status_t status = io_apic.Write(kIoApicIoRegSel, IoValue::FromU8(reg));
  if (status != ZX_OK) {
    return status;
  }
  return io_apic.Read(kIoApicIoWin, result);
}

// Write the given indirect register from the IO APIC.
zx_status_t WriteRegister(IoApic& io_apic, uint8_t reg, const IoValue& value) {
  zx_status_t status = io_apic.Write(kIoApicIoRegSel, IoValue::FromU8(reg));
  if (status != ZX_OK) {
    return status;
  }
  return io_apic.Write(kIoApicIoWin, value);
}

TEST(IoApic, IndirectRegisterSelect) {
  Guest guest;
  IoApic io_apic(&guest);

  // An arbitrary multiplier to expand an 8-bit index to fill 32-bits, helping detect
  // endian issues or short reads/writes.
  constexpr uint32_t kMultiplier = 0x04030201;

  // Write to all the redirection registers.
  for (uint8_t i = kFirstRedirectOffset; i <= kLastRedirectOffset; i++) {
    // Select the register.
    ASSERT_EQ(io_apic.Write(kIoApicIoRegSel, IoValue::FromU8(i)), ZX_OK);

    // Read the selected register value back again.
    IoValue value = IoValue::FromU8(0);
    ASSERT_EQ(io_apic.Read(kIoApicIoRegSel, &value), ZX_OK);
    EXPECT_EQ(value.u8, i);

    // Write a 32-bit value to it.
    ASSERT_EQ(io_apic.Write(kIoApicIoWin, IoValue{.access_size = 4, .u32 = i * kMultiplier}),
              ZX_OK);
  }

  // Verify the values were retained.
  for (uint8_t i = kFirstRedirectOffset; i <= kLastRedirectOffset; i++) {
    // Select the register.
    ASSERT_EQ(io_apic.Write(kIoApicIoRegSel, IoValue::FromU8(i)), ZX_OK);

    // Verify the 32-bit value to it.
    IoValue value = IoValue::FromU32(0);
    ASSERT_EQ(io_apic.Read(kIoApicIoWin, &value), ZX_OK);
    EXPECT_EQ(value.u32, i * kMultiplier);
  }
}

TEST(IoApic, InvalidReadWriteSize) {
  Guest guest;
  IoApic io_apic(&guest);

  // Select a redirect entry.
  ASSERT_EQ(io_apic.Write(kIoApicIoRegSel, IoValue::FromU8(kFirstRedirectOffset)), ZX_OK);

  // Attempt to read 8/16 bit values. We expect an error.
  IoValue result8 = IoValue::FromU8(0);
  EXPECT_EQ(io_apic.Read(kIoApicIoWin, &result8), ZX_ERR_NOT_SUPPORTED);
  IoValue result16 = IoValue::FromU16(0);
  EXPECT_EQ(io_apic.Read(kIoApicIoWin, &result16), ZX_ERR_NOT_SUPPORTED);

  // Attempt to write 8/16 bit values. We expect an error.
  EXPECT_EQ(io_apic.Write(kIoApicIoWin, IoValue::FromU8(0xff)), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(io_apic.Write(kIoApicIoWin, IoValue::FromU16(0xffff)), ZX_ERR_NOT_SUPPORTED);
}

TEST(IoApic, ApicId) {
  Guest guest;
  IoApic io_apic(&guest);

  // APIC ID should start as 0. (Intel ICH10, Section 13.5.5).
  IoValue value = IoValue::FromU32(0);
  ASSERT_EQ(ReadRegister(io_apic, kIoApicRegisterId, &value), ZX_OK);
  EXPECT_EQ(value.u32, 0u);

  // Write a value.
  ASSERT_EQ(WriteRegister(io_apic, kIoApicRegisterId, IoValue::FromU32(0x11223344)), ZX_OK);

  // Ensure the value persists.
  value = IoValue::FromU32(0);
  ASSERT_EQ(ReadRegister(io_apic, kIoApicRegisterId, &value), ZX_OK);
  EXPECT_EQ(value.u32, 0x11223344u);
}

TEST(IoApic, Version) {
  Guest guest;
  IoApic io_apic(&guest);

  // Read the version register.
  IoValue value = IoValue::FromU32(0);
  ASSERT_EQ(ReadRegister(io_apic, kIoApicRegisterVer, &value), ZX_OK);
  EXPECT_EQ(value.u32, 0x002f'0020u);  // 0x2f interrupts, APIC version 0x20.
}

TEST(IoApic, EndOfInterrupt) {
  Guest guest;
  IoApic io_apic(&guest);

  // Write to the EOI register. We don't expect any effect.
  ASSERT_EQ(io_apic.Write(kIoApicEOIR, IoValue::FromU32(0x01)), ZX_OK);
}

}  // namespace
