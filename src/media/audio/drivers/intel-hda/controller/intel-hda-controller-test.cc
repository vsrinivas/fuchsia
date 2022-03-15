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
    pcie_device_info_t info = {.vendor_id = INTEL_HDA_PCI_VID,
                               .device_id = INTEL_HDA_PCI_DID_KABYLAKE};
    pci_.SetDeviceInfo(info);
    zx_vaddr_t vaddr = {};
    ASSERT_OK(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                                         kHdaBar0Size, &vaddr));
    // We now fake part of the BAR 0 register values.
    // We fake at least those needed by the reset of the controller hardware.
    auto regs = reinterpret_cast<hda_all_registers_t*>(vaddr);
    regs->regs.vmaj = 0x01;
    regs->regs.vmin = 0x00;
    parent_ = MockDevice::FakeRootParent();
    parent_->AddProtocol(ZX_PROTOCOL_PCI, pci_.get_protocol().ops, pci_.get_protocol().ctx, "pci");
  }

  MockDevice* parent() const { return parent_.get(); }
  pci::FakePciProtocol& pci() { return pci_; }

 private:
  static constexpr size_t kHdaBar0Size = 0x4000;

  pci::FakePciProtocol pci_;
  std::shared_ptr<MockDevice> parent_;
};

TEST_F(HdaControllerTest, HardwareResetMiscellaneousBackboneDynamicClockGatingEnable) {
  acpi::mock::Device mock_acpi;
  async::Loop acpi_async_loop(&kAsyncLoopConfigNeverAttachToThread);
  auto acpi_client = mock_acpi.CreateClient(acpi_async_loop.dispatcher());
  TestIntelHDAController controller(std::move(acpi_client.value()));

  auto config = pci().GetConfigVmo();
  ASSERT_OK(controller.SetupPCIDevice(parent()));

  // Before resetting the controller HW the BDCGE bit is not set.
  uint32_t val = 0;
  config->read(&val, kPciRegCgctl, sizeof(uint32_t));
  ASSERT_EQ(val, 0);

  ASSERT_OK(controller.ResetControllerHardware());

  // After resetting the controller HW the BDCGE bit is set.
  config->read(&val, kPciRegCgctl, sizeof(uint32_t));
  ASSERT_EQ(val, kPciRegCgctlBitMaskMiscbdcge);
}

}  // namespace audio::intel_hda
