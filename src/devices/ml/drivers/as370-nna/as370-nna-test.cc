// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-nna.h"

#include <fuchsia/hardware/registers/cpp/banjo-mock.h>
#include <lib/async-loop/cpp/loop.h>

#include "src/devices/lib/as370/include/soc/as370/as370-nna.h"
#include "src/devices/registers/testing/mock-registers/mock-registers.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace as370_nna {

class MockRegisters {
 public:
  MockRegisters() {
    loop_.StartThread();
    global_registers_mock_ =
        std::make_unique<mock_registers::MockRegistersDevice>(loop_.dispatcher());
  }

  // The caller should set the mock expectations before calling this.
  void CreateDeviceAndVerify() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_registers::Device>();
    EXPECT_OK(endpoints.status_value());

    global_registers_mock_->RegistersConnect(endpoints->server.TakeChannel());

    ddk::PDev pdev;
    auto device = std::make_unique<As370NnaDevice>(
        fake_parent_.get(), fidl::WireSyncClient(std::move(endpoints->client)), std::move(pdev));
    ASSERT_NOT_NULL(device);
    EXPECT_OK(device->Init());

    EXPECT_OK(GlobalRegisters()->VerifyAll());

    loop_.Shutdown();
  }

  mock_registers::MockRegisters* GlobalRegisters() {
    return global_registers_mock_->fidl_service();
  }

  std::shared_ptr<MockDevice> fake_parent_ = MockDevice::FakeRootParent();
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  std::unique_ptr<mock_registers::MockRegistersDevice> global_registers_mock_;
};

TEST(As370NnaTest, Init) {
  MockRegisters mock_regs;

  mock_regs.GlobalRegisters()->ExpectWrite<uint32_t>(as370::kNnaResetOffset, as370::kNnaResetMask,
                                                     0);
  mock_regs.GlobalRegisters()->ExpectWrite<uint32_t>(as370::kNnaResetOffset, as370::kNnaResetMask,
                                                     1);
  mock_regs.GlobalRegisters()->ExpectWrite<uint32_t>(as370::kNnaPowerOffset, as370::kNnaPowerMask,
                                                     0);
  mock_regs.GlobalRegisters()->ExpectWrite<uint32_t>(as370::kNnaClockSysOffset,
                                                     as370::kNnaClockSysMask, 0x299);
  mock_regs.GlobalRegisters()->ExpectWrite<uint32_t>(as370::kNnaClockCoreOffset,
                                                     as370::kNnaClockCoreMask, 0x299);

  ASSERT_NO_FATAL_FAILURE(mock_regs.CreateDeviceAndVerify());
}

}  // namespace as370_nna
