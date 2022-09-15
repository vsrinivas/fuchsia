// Copyright 2022 The Fuchsia Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-hda-controller.h"

#include <lib/async-loop/cpp/loop.h>

#include <zxtest/zxtest.h>

#include "device-ids.h"
#include "pci_regs.h"
#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/pci/testing/pci_protocol_fake.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace audio::intel_hda {

struct TestIntelHDAController : public IntelHDAController {
  explicit TestIntelHDAController(acpi::Client client) : IntelHDAController(std::move(client)) {}
  zx_status_t ResetControllerHardware() { return IntelHDAController::ResetControllerHardware(); }
  zx_status_t SetupPCIDevice(zx_device_t* pci_dev) {
    return IntelHDAController::SetupPCIDevice(pci_dev);
  }
};

class HdaControllerTest : public zxtest::Test {
 protected:
  void SetUp() final {
    auto& vmo = pci_.CreateBar(0, kHdaBar0Size, true);
    fuchsia_hardware_pci::wire::DeviceInfo info = {.vendor_id = INTEL_HDA_PCI_VID,
                                                   .device_id = INTEL_HDA_PCI_DID_KABYLAKE};
    pci_.SetDeviceInfo(info);
    zx_vaddr_t vaddr = {};
    ASSERT_OK(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                                         kHdaBar0Size, &vaddr));
    // We now fake part of the BAR 0 register values.
    // We fake at least those needed by the reset of the controller hardware.
    regs_ = reinterpret_cast<hda_all_registers_t*>(vaddr);
    regs_->regs.vmaj = 0x01;
    regs_->regs.vmin = 0x00;

    config_vmo_ = pci_.GetConfigVmo();

    parent_ = MockDevice::FakeRootParent();
    parent_->AddFidlProtocol(
        fidl::DiscoverableProtocolName<fuchsia_hardware_pci::Device>,
        [this](zx::channel channel) {
          fidl::BindServer(loop_.dispatcher(),
                           fidl::ServerEnd<fuchsia_hardware_pci::Device>(std::move(channel)),
                           &pci_);
          return ZX_OK;
        },
        "pci");
    loop_.StartThread("pci-fidl-server-thread");
  }

  MockDevice* parent() const { return parent_.get(); }
  hda_all_registers_t* regs() { return regs_; }
  zx::unowned_vmo& config() { return config_vmo_; }

 private:
  static constexpr size_t kHdaBar0Size = 0x4000;

  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  pci::FakePciProtocol pci_;
  std::shared_ptr<MockDevice> parent_;
  hda_all_registers_t* regs_;
  zx::unowned_vmo config_vmo_;
};

TEST_F(HdaControllerTest, HardwareResetMiscellaneousBackboneDynamicClockGatingEnable) {
  acpi::mock::Device mock_acpi;
  async::Loop acpi_async_loop(&kAsyncLoopConfigNeverAttachToThread);
  auto acpi_client = mock_acpi.CreateClient(acpi_async_loop.dispatcher());
  TestIntelHDAController controller(std::move(acpi_client.value()));

  ASSERT_OK(controller.SetupPCIDevice(parent()));

  // Before resetting the controller HW the BDCGE bit is not set.
  uint32_t val = 0;
  config()->read(&val, kPciRegCgctl, sizeof(uint32_t));
  ASSERT_EQ(val, 0);

  ASSERT_OK(controller.ResetControllerHardware());

  // After resetting the controller HW the BDCGE bit is set.
  config()->read(&val, kPciRegCgctl, sizeof(uint32_t));
  ASSERT_EQ(val, kPciRegCgctlBitMaskMiscbdcge);
}

TEST_F(HdaControllerTest, HardwareResetStateSts) {
  acpi::mock::Device mock_acpi;
  async::Loop acpi_async_loop(&kAsyncLoopConfigNeverAttachToThread);
  auto acpi_client = mock_acpi.CreateClient(acpi_async_loop.dispatcher());
  TestIntelHDAController controller(std::move(acpi_client.value()));

  // We set the HW to initially not be in reset.
  // During HW reset the driver will clear this bit and wait until it is cleared in the hardware.
  // Similarly the driver will later set this bit to take the HW out of reset and wait until the
  // bit is set in the HW.
  // During this test the bit is changed in memory as the driver sets or clears it, so the wait is
  // virtually gone.
  regs()->regs.gctl = HDA_REG_GCTL_HWINIT;

  // Before resetting the controller HW the STATUSSTS register is not cleared.
  ASSERT_EQ(regs()->regs.statests, 0);

  ASSERT_OK(controller.SetupPCIDevice(parent()));
  ASSERT_OK(controller.ResetControllerHardware());

  // After resetting the controller HW the STATUSSTS register is cleared.
  ASSERT_EQ(regs()->regs.statests, HDA_REG_STATESTS_MASK);
}

TEST_F(HdaControllerTest, HardwareResetError) {
  acpi::mock::Device mock_acpi;
  async::Loop acpi_async_loop(&kAsyncLoopConfigNeverAttachToThread);
  auto acpi_client = mock_acpi.CreateClient(acpi_async_loop.dispatcher());
  TestIntelHDAController controller(std::move(acpi_client.value()));

  // We set the HDA HW version to be incorrect such that reset fails even after retries.
  regs()->regs.vmaj = 0x99;

  ASSERT_OK(controller.SetupPCIDevice(parent()));
  ASSERT_EQ(controller.ResetControllerHardware(), ZX_ERR_NOT_SUPPORTED);
}

}  // namespace audio::intel_hda
