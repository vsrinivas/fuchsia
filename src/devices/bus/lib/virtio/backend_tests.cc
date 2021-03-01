// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/virtio/driver_utils.h>
#include <lib/virtio/ring.h>
#include <lib/zx/bti.h>
#include <zircon/errors.h>
#include <zircon/hw/pci.h>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"
#include "src/devices/testing/fake_ddk/include/lib/fake_ddk/fake_ddk.h"

// This capability configuration comes straight from a Virtio
// device running inside FEMU.
constexpr uint8_t kCapabilityOffsets[5] = {0x84, 0x70, 0x60, 0x50, 0x40};
// clang-format off
constexpr virtio_pci_cap_t kCapabilities[5] = {
  {
      .cap_vndr = 0x9,
      .cap_next = 0x70,
      .cap_len = 0x14,
      .cfg_type = VIRTIO_PCI_CAP_PCI_CFG,
      .bar = 0,
      .offset = 0,
      .length = 0,
  },
  {

      .cap_vndr = 0x9,
      .cap_next = 0x60,
      .cap_len = 0x14,
      .cfg_type = VIRTIO_PCI_CAP_NOTIFY_CFG,
      .bar = 4,
      .offset = 0x3000,
      .length = 0x1000,
  },
  {
      .cap_vndr = 0x9,
      .cap_next = 0x50,
      .cap_len = 0x10,
      .cfg_type = VIRTIO_PCI_CAP_DEVICE_CFG,
      .bar = 4,
      .offset = 0x2000,
      .length = 0x1000,
  },
  {
      .cap_vndr = 0x9,
      .cap_next = 0x40,
      .cap_len = 0x10,
      .cfg_type = VIRTIO_PCI_CAP_ISR_CFG,
      .bar = 4,
      .offset = 0x1000,
      .length = 0x1000,

  },
  {
      .cap_vndr = 0x9,
      .cap_next = 0x0,
      .cap_len = 0x10,
      .cfg_type = VIRTIO_PCI_CAP_COMMON_CFG,
      .bar = 4,
      .offset = 0x0000,
      .length = 0x1000,
  }
};
// clang-format on

fx_log_severity_t kTestLogLevel = FX_LOG_INFO;
class VirtioTests : public fake_ddk::Bind, public zxtest::Test {
 protected:
  void SetUp() final { fake_ddk::kMinLogSeverity = kTestLogLevel; }

  void TearDown() final { fake_pci_.reset(); }
  pci::FakePciProtocol& fake_pci() { return fake_pci_; }
  void CleanUp() {
    DeviceAsyncRemove(fake_ddk::kFakeDevice);
    EXPECT_TRUE(Ok());
  }
  void SetUpProtocol() { SetProtocol(ZX_PROTOCOL_PCI, &fake_pci_.get_protocol()); }
  void SetUpBars() {
    zx::vmo vmo;
    uint64_t size = 0x4000;  // Capabilities need up to offset 0x3000 + 0x1000.
    ASSERT_OK(zx::vmo::create(size, 0, &vmo));
    fake_pci_.SetBar(4, size, std::move(vmo));
  }

  void SetUpCapabilities() {
    for (uint32_t i = 0; i < countof(kCapabilities); i++) {
      fake_pci_.AddVendorCapability(kCapabilityOffsets[i], kCapabilities[i].cap_len);
    }

    auto config = fake_pci().GetConfigVmo();
    for (uint32_t i = 0; i < countof(kCapabilities); i++) {
      config->write(&kCapabilities[i], kCapabilityOffsets[i], sizeof(virtio_pci_cap_t));
    }
  }

  void SetUpQueue() {
    auto& common_cfg_cap = kCapabilities[4];
    zx::unowned_vmo bar = fake_pci().GetBar(common_cfg_cap.bar);
    uint16_t queue_size = 0x2;
    uint32_t queue_offset = offsetof(virtio_pci_common_cfg_t, queue_size);
    bar->write(&queue_size, common_cfg_cap.offset + queue_offset, sizeof(queue_size));
  }

  void SetUpMsiX() {
    fake_pci_.AddMsixInterrupt();
    fake_pci_.AddMsixInterrupt();

    // Virtio stores a configuration register for MSI-X in a field in the common
    // configuration capability. We use the structures above to figure out what
    // bar that is in, and what offset it's at.
    auto& common_cfg_cap = kCapabilities[4];
    zx::unowned_vmo bar = fake_pci().GetBar(common_cfg_cap.bar);
    uint16_t msix_config_val = 0xFFFF;  // Set to no MSI-X vector allocated.
    uint32_t msix_offset = offsetof(virtio_pci_common_cfg_t, msix_config);
    bar->write(&msix_config_val, common_cfg_cap.offset + msix_offset, sizeof(msix_config_val));
  }

 private:
  pci::FakePciProtocol fake_pci_;
};

class TestVirtioDevice;
using DeviceType = ddk::Device<TestVirtioDevice, ddk::Unbindable>;
class TestVirtioDevice : public virtio::Device, public DeviceType {
 public:
  explicit TestVirtioDevice(zx_device_t* bus_device, zx::bti bti,
                            std::unique_ptr<virtio::Backend> backend)
      : virtio::Device(bus_device, std::move(bti), std::move(backend)), DeviceType(bus_device){};
  zx_status_t Init() final {
    zx_status_t status = ring_.Init(0);
    if (status != ZX_OK) {
      return status;
    }
    return DdkAdd(tag());
  }
  void IrqRingUpdate() final{};
  void IrqConfigChange() final{};
  const char* tag() const final { return "test"; }
  void DdkUnbind(ddk::UnbindTxn txn) {
    txn.Reply();
    DdkRelease();
  }
  void DdkRelease() { delete this; }

 private:
  virtio::Ring ring_ = {this};
};

TEST_F(VirtioTests, FailureNoProtocol) {
  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
}

TEST_F(VirtioTests, FailureNoCapabilities) {
  SetUpProtocol();
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
            virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
}

TEST_F(VirtioTests, FailureNoBar) {
  SetUpProtocol();
  SetUpCapabilities();
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
            virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
}

TEST_F(VirtioTests, LegacyInterruptBindSuccess) {
  SetUpProtocol();
  SetUpCapabilities();
  SetUpBars();
  SetUpQueue();
  fake_pci().AddLegacyInterrupt();

  ASSERT_OK(virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
  CleanUp();
}

TEST_F(VirtioTests, FailureOneMsixBind) {
  fake_pci().AddMsixInterrupt();
  SetUpProtocol();
  SetUpCapabilities();
  SetUpBars();
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
            virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
}

TEST_F(VirtioTests, TwoMsixBindSuccess) {
  SetUpProtocol();
  SetUpCapabilities();
  SetUpBars();
  SetUpQueue();
  SetUpMsiX();

  // With everything set up this should succeed.
  ASSERT_OK(virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
  CleanUp();
}

int main(int argc, char* argv[]) {
  int opt;
  int v_position = 0;
  while (!v_position && (opt = getopt(argc, argv, "hv")) != -1) {
    switch (opt) {
      case 'v':
        if (kTestLogLevel > FX_LOG_TRACE) {
          kTestLogLevel -= FX_LOG_SEVERITY_STEP_SIZE;
        }

        v_position = optind - 1;
        break;
      case 'h':
        fprintf(stderr,
                "    Test-Specific Usage: %s [OPTIONS]\n\n"
                "    [OPTIONS]\n"
                "    -v                                                  Enable DEBUG logs\n"
                "    -vv                                                 Enable TRACE logs\n\n",
                argv[0]);
        break;
    }
  }

  // Do the minimal amount of work to forward the rest of the args to zxtest
  // with our consumed '-v' / '-vv' removed. Don't worry about additional -v
  // usage because the zxtest help will point out the invalid nature of it.
  if (v_position) {
    for (int p = v_position; p < argc - 1; p++) {
      argv[p] = argv[p + 1];
    }
    argc--;
  }
  return RUN_ALL_TESTS(argc, argv);
}
