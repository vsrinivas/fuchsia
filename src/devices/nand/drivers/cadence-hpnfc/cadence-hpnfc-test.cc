// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cadence-hpnfc.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <limits.h>

#include <fbl/algorithm.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/as370/as370-nand.h>
#include <zxtest/zxtest.h>

namespace rawnand {

TEST(CadenceHpnfcTest, DdkLifecycle) {
  ddk_mock::MockMmioReg mmio_array[as370::kNandSize / sizeof(uint32_t)];
  ddk_mock::MockMmioRegRegion mmio_regs(mmio_array, sizeof(uint32_t), fbl::count_of(mmio_array));

  ddk_mock::MockMmioReg fifo_mmio_array[1];
  ddk_mock::MockMmioRegRegion fifo_mmio_regs(fifo_mmio_array, sizeof(uint32_t),
                                             fbl::count_of(fifo_mmio_array));

  zx::interrupt interrupt;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &interrupt));

  ddk::MmioBuffer buffer(mmio_regs.GetMmioBuffer());
  CadenceHpnfc dut(fake_ddk::kFakeParent, ddk::MmioBuffer(mmio_regs.GetMmioBuffer()),
                   ddk::MmioBuffer(fifo_mmio_regs.GetMmioBuffer()), std::move(interrupt));

  fake_ddk::Bind ddk;

  EXPECT_OK(dut.StartInterruptThread());
  EXPECT_OK(dut.Bind());
  dut.DdkAsyncRemove();

  EXPECT_TRUE(ddk.Ok());
}

}  // namespace rawnand
