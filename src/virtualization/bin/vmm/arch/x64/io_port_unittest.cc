// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/io_port.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/virtualization/bin/vmm/bits.h"
#include "src/virtualization/bin/vmm/guest.h"

namespace {

TEST(CmosHandler, WriteReadBootByte) {
  Guest guest;
  CmosHandler cmos_handler;
  cmos_handler.Init(&guest);

  // Set the BootByte address as the current index, and set bit 0x80 which is the NMI disable bit.
  // The NMI disable bit should be ignored when determining the CMOS index.
  zx_status_t status =
      cmos_handler.Write(kCmosIndexPort, IoValue::FromU8(0x80 | kCmosRebootReason));
  ASSERT_EQ(status, ZX_OK);

  // Sentinel as we are expecting 0 back on a good read
  IoValue result = IoValue::FromU8(-1);
  status = cmos_handler.Read(kCmosDataPort, &result);
  ASSERT_EQ(status, ZX_OK);

  ASSERT_EQ(result.u8, 0);
  ASSERT_EQ(result.access_size, 1);

  // See if we can write a non zero value to the address
  status = cmos_handler.Write(kCmosDataPort, IoValue::FromU8(1));
  ASSERT_EQ(status, ZX_OK);

  // See if we can recover what we wrote, default is 0
  status = cmos_handler.Read(kCmosDataPort, &result);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(result.u8, 1);
  ASSERT_EQ(result.access_size, 1);
}

TEST(I8042Handler, SettingByteShutsdownVcpu) {
  Guest guest;
  I8042Handler handler;
  handler.Init(&guest);

  // Writing to the data port just returns ZX_OK.
  ASSERT_EQ(handler.Write(0x0, IoValue::FromU8(0xfe)), ZX_OK);

  // Writing to the command port shuts down the VCPU by returning ZX_ERR_CANCELED (which will then
  // terminate the guest and VM object).
  ASSERT_EQ(handler.Write(0x4, IoValue::FromU8(0xfe)), ZX_ERR_CANCELED);
}

}  // namespace
