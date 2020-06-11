// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-nna.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>

#include <mock-mmio-reg/mock-mmio-reg.h>

namespace {
constexpr size_t kHiuRegSize = 0x2000 / sizeof(uint32_t);
constexpr size_t kPowerRegSize = 0x1000 / sizeof(uint32_t);
constexpr size_t kMemoryPDRegSize = 0x1000 / sizeof(uint32_t);
}  // namespace

namespace aml_nna {

TEST(AmlNnaTest, Init) {
  auto hiu_regs = std::make_unique<ddk_mock::MockMmioReg[]>(kHiuRegSize);
  ddk_mock::MockMmioRegRegion hiu_mock(hiu_regs.get(), sizeof(uint32_t), kHiuRegSize);

  auto power_regs = std::make_unique<ddk_mock::MockMmioReg[]>(kPowerRegSize);
  ddk_mock::MockMmioRegRegion power_mock(power_regs.get(), sizeof(uint32_t), kPowerRegSize);

  auto memory_pd_regs = std::make_unique<ddk_mock::MockMmioReg[]>(kMemoryPDRegSize);
  ddk_mock::MockMmioRegRegion memory_pd_mock(memory_pd_regs.get(), sizeof(uint32_t),
                                             kMemoryPDRegSize);

  power_regs[0x3a].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFCFFFF);
  power_regs[0x3b].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFCFFFF);

  memory_pd_regs[0x43].ExpectWrite(0);
  memory_pd_regs[0x44].ExpectWrite(0);

  hiu_regs[0x72].ExpectRead(0x00000000).ExpectWrite(0x700);
  hiu_regs[0x72].ExpectRead(0x00000000).ExpectWrite(0x7000000);

  pdev_protocol_t proto;
  auto device = std::make_unique<AmlNnaDevice>(fake_ddk::kFakeParent, hiu_mock.GetMmioBuffer(),
                                               power_mock.GetMmioBuffer(),
                                               memory_pd_mock.GetMmioBuffer(), proto);
  ASSERT_NOT_NULL(device);
  device->Init();

  hiu_mock.VerifyAll();
  power_mock.VerifyAll();
  memory_pd_mock.VerifyAll();
}

}  // namespace aml_nna
