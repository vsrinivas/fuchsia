// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-nna.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/registers.h>

#include "s905d3-nna-regs.h"
#include "src/devices/registers/testing/mock-registers/mock-registers.h"
#include "t931-nna-regs.h"

namespace {
constexpr size_t kHiuRegSize = 0x2000 / sizeof(uint32_t);
constexpr size_t kPowerRegSize = 0x1000 / sizeof(uint32_t);
constexpr size_t kMemoryPDRegSize = 0x1000 / sizeof(uint32_t);
}  // namespace

namespace aml_nna {

class MockRegisters {
 public:
  MockRegisters()
      : hiu_regs_(std::make_unique<ddk_mock::MockMmioReg[]>(kHiuRegSize)),
        power_regs_(std::make_unique<ddk_mock::MockMmioReg[]>(kPowerRegSize)),
        memory_pd_regs_(std::make_unique<ddk_mock::MockMmioReg[]>(kMemoryPDRegSize)),

        hiu_mock_(ddk_mock::MockMmioRegRegion(hiu_regs_.get(), sizeof(uint32_t), kHiuRegSize)),
        power_mock_(
            ddk_mock::MockMmioRegRegion(power_regs_.get(), sizeof(uint32_t), kPowerRegSize)),
        memory_pd_mock_(ddk_mock::MockMmioRegRegion(memory_pd_regs_.get(), sizeof(uint32_t),
                                                    kMemoryPDRegSize)) {
    loop_.StartThread();
    reset_mock_ = std::make_unique<mock_registers::MockRegistersDevice>(loop_.dispatcher());
  }

  // The caller should set the mock expectations before calling this.
  void CreateDeviceAndVerify(AmlNnaDevice::NnaBlock nna_block) {
    zx::channel client_end, server_end;
    EXPECT_OK(zx::channel::create(0, &client_end, &server_end));
    reset_mock_->RegistersConnect(std::move(server_end));

    ddk::PDev pdev;
    auto device = std::make_unique<AmlNnaDevice>(
        fake_ddk::kFakeParent, hiu_mock_.GetMmioBuffer(), power_mock_.GetMmioBuffer(),
        memory_pd_mock_.GetMmioBuffer(), std::move(client_end), std::move(pdev), nna_block);
    ASSERT_NOT_NULL(device);
    EXPECT_OK(device->Init());

    hiu_mock_.VerifyAll();
    power_mock_.VerifyAll();
    memory_pd_mock_.VerifyAll();
    EXPECT_OK(reset()->VerifyAll());

    loop_.Shutdown();
  }

  mock_registers::MockRegisters* reset() { return reset_mock_->fidl_service(); }

  std::unique_ptr<ddk_mock::MockMmioReg[]> hiu_regs_;
  std::unique_ptr<ddk_mock::MockMmioReg[]> power_regs_;
  std::unique_ptr<ddk_mock::MockMmioReg[]> memory_pd_regs_;

  ddk_mock::MockMmioRegRegion hiu_mock_;
  ddk_mock::MockMmioRegRegion power_mock_;
  ddk_mock::MockMmioRegRegion memory_pd_mock_;

  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  std::unique_ptr<mock_registers::MockRegistersDevice> reset_mock_;
};

TEST(AmlNnaTest, InitT931) {
  MockRegisters mock_regs;

  mock_regs.power_regs_[0x3a].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFCFFFF);
  mock_regs.power_regs_[0x3b].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFCFFFF);

  mock_regs.memory_pd_regs_[0x43].ExpectWrite(0);
  mock_regs.memory_pd_regs_[0x44].ExpectWrite(0);

  mock_regs.reset()->ExpectWrite<uint32_t>(0x88, 1 << 12, 0);
  mock_regs.reset()->ExpectWrite<uint32_t>(0x88, 1 << 12, 1 << 12);

  mock_regs.hiu_regs_[0x72].ExpectRead(0x00000000).ExpectWrite(0x700);
  mock_regs.hiu_regs_[0x72].ExpectRead(0x00000000).ExpectWrite(0x7000000);

  ASSERT_NO_FATAL_FAILURES(mock_regs.CreateDeviceAndVerify(T931NnaBlock));
}

TEST(AmlNnaTest, InitS905d3) {
  MockRegisters mock_regs;

  mock_regs.power_regs_[0x3a].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFEFFFF);
  mock_regs.power_regs_[0x3b].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFEFFFF);

  mock_regs.memory_pd_regs_[0x46].ExpectWrite(0);
  mock_regs.memory_pd_regs_[0x47].ExpectWrite(0);

  mock_regs.reset()->ExpectWrite<uint32_t>(0x88, 1 << 12, 0);
  mock_regs.reset()->ExpectWrite<uint32_t>(0x88, 1 << 12, 1 << 12);

  mock_regs.hiu_regs_[0x72].ExpectRead(0x00000000).ExpectWrite(0x700);
  mock_regs.hiu_regs_[0x72].ExpectRead(0x00000000).ExpectWrite(0x7000000);

  ASSERT_NO_FATAL_FAILURES(mock_regs.CreateDeviceAndVerify(S905d3NnaBlock));
}

}  // namespace aml_nna
