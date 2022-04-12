// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cadence-hpnfc.h"

#include <limits.h>
#include <zircon/time.h>

#include <fbl/algorithm.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace rawnand {

// Pulled from the as370 library.
constexpr uint32_t kNandSize = fbl::round_up<uint32_t, uint32_t>(0x2084, PAGE_SIZE);

TEST(CadenceHpnfcTest, DdkLifecycle) {
  ddk_mock::MockMmioReg mmio_array[kNandSize / sizeof(uint32_t)];
  ddk_mock::MockMmioRegRegion mmio_regs(mmio_array, sizeof(uint32_t), std::size(mmio_array));

  ddk_mock::MockMmioReg fifo_mmio_array[1];
  ddk_mock::MockMmioRegRegion fifo_mmio_regs(fifo_mmio_array, sizeof(uint32_t),
                                             std::size(fifo_mmio_array));

  zx::interrupt interrupt;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &interrupt));

  ddk::MmioBuffer buffer(mmio_regs.GetMmioBuffer());
  std::shared_ptr<zx_device> fake_root_ = MockDevice::FakeRootParent();
  auto* dut =
      new CadenceHpnfc(fake_root_.get(), ddk::MmioBuffer(mmio_regs.GetMmioBuffer()),
                       ddk::MmioBuffer(fifo_mmio_regs.GetMmioBuffer()), std::move(interrupt));

  EXPECT_OK(dut->StartInterruptThread());
  EXPECT_OK(dut->Bind());
  dut->DdkAsyncRemove();
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
}

}  // namespace rawnand
