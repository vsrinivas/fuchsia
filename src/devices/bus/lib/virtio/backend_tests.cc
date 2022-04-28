// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/pci.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>
#include <lib/virtio/backends/pci.h>
#include <lib/virtio/driver_utils.h>
#include <lib/virtio/ring.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/hw/pci.h>
#include <zircon/status.h>

#include <ddktl/device.h>
#include <virtio/virtio.h>
#include <zxtest/zxtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"

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

class TestLegacyIoInterface : public virtio::PciLegacyIoInterface {
 public:
  explicit TestLegacyIoInterface(fdf::MmioView view) : view_(view) {}
  ~TestLegacyIoInterface() override = default;

  void Read(uint16_t offset, uint8_t* val) const final {
    *val = view_.Read8(offset);
    zxlogf(TRACE, "%s: %#x -> %#x", __PRETTY_FUNCTION__, offset, *val);
  }
  void Read(uint16_t offset, uint16_t* val) const final {
    *val = view_.Read16(offset);
    zxlogf(TRACE, "%s: %#x -> %#x", __PRETTY_FUNCTION__, offset, *val);
  }
  void Read(uint16_t offset, uint32_t* val) const final {
    *val = view_.Read32(offset);
    zxlogf(TRACE, "%s: %#x -> %#x", __PRETTY_FUNCTION__, offset, *val);
  }
  void Write(uint16_t offset, uint8_t val) const final {
    view_.Write8(val, offset);
    zxlogf(TRACE, "%s: %#x <- %#x", __PRETTY_FUNCTION__, offset, val);
  }
  void Write(uint16_t offset, uint16_t val) const final {
    view_.Write16(val, offset);
    zxlogf(TRACE, "%s: %#x <- %#x", __PRETTY_FUNCTION__, offset, val);
  }
  void Write(uint16_t offset, uint32_t val) const final {
    view_.Write32(val, offset);
    zxlogf(TRACE, "%s: %#x <- %#x", __PRETTY_FUNCTION__, offset, val);
  }

 private:
  fdf::MmioView view_;
};

fx_log_severity_t kTestLogLevel = FX_LOG_INFO;
class VirtioTests : public fake_ddk::Bind, public zxtest::Test {
 public:
  static constexpr uint16_t kQueueSize = 1u;
  static constexpr uint32_t kLegacyBar = 0u;
  static constexpr uint32_t kModernBar = 4u;

 protected:
  void SetUp() final { fake_ddk::kMinLogSeverity = kTestLogLevel; }

  void TearDown() final { fake_pci_.Reset(); }
  pci::FakePciProtocol& fake_pci() { return fake_pci_; }
  std::array<std::optional<fdf::MmioBuffer>, PCI_MAX_BAR_REGS>& bars() { return bars_; }

  void CleanUp() {
    DeviceAsyncRemove(fake_ddk::kFakeDevice);
    EXPECT_TRUE(Ok());
  }
  void SetUpProtocol() { SetProtocol(ZX_PROTOCOL_PCI, &fake_pci_.get_protocol()); }
  void SetUpModernBars() {
    size_t bar_size = 0x3000 + 0x1000;  // 0x3000 is the offset of the last capability in the bar,
                                        // and 0x1000 is the length.
    fake_pci_.CreateBar(kModernBar, bar_size, /*is_mmio=*/true);

    ddk::Pci pci(fake_pci().get_protocol());
    ZX_ASSERT(pci.is_valid());
    ZX_ASSERT(pci.MapMmio(kModernBar, ZX_CACHE_POLICY_UNCACHED_DEVICE, &bars_[kModernBar]) ==
              ZX_OK);
  }

  void SetUpModernCapabilities() {
    for (uint32_t i = 0; i < std::size(kCapabilities); i++) {
      fake_pci_.AddVendorCapability(kCapabilityOffsets[i], kCapabilities[i].cap_len);
    }

    auto config = fake_pci().GetConfigVmo();
    for (uint32_t i = 0; i < std::size(kCapabilities); i++) {
      config->write(&kCapabilities[i], kCapabilityOffsets[i], sizeof(virtio_pci_cap_t));
    }
  }

  void SetUpModernQueue() {
    auto& common_cfg_cap = kCapabilities[4];
    uint32_t queue_offset = offsetof(virtio_pci_common_cfg_t, queue_size);
    bars_[kModernBar]->Write(kQueueSize, common_cfg_cap.offset + queue_offset);
  }

  void SetUpModernMsiX() {
    fake_pci_.AddMsixInterrupt();
    fake_pci_.AddMsixInterrupt();

    // Virtio stores a configuration register for MSI-X in a field in the common
    // configuration capability. We use the structures above to figure out what
    // bar that is in, and what offset it's at.
    auto& common_cfg_cap = kCapabilities[4];
    uint16_t msix_config_val = 0xFFFF;  // Set to no MSI-X vector allocated.
    uint32_t msix_offset = offsetof(virtio_pci_common_cfg_t, config_msix_vector);
    bars_[kModernBar]->Write(msix_config_val, common_cfg_cap.offset + msix_offset);
  }

  void SetUpLegacyBar() {
    size_t bar_size = 0x64;  // Matches the bar size on GCE for Bar0.
    fake_pci_.CreateBar(kLegacyBar, bar_size, /*is_mmio=*/false);

    // Legacy BARs identified as IO in PCI cannot be mapped by pci::MapMmio, so we need to do it by
    // hand.
    zx::vmo vmo{};
    ZX_ASSERT(fake_pci().GetBar(kLegacyBar).duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo) == ZX_OK);
    size_t size = 0;
    vmo.get_size(&size);
    zx_status_t status = fdf::MmioBuffer::Create(
        0, size, std::move(vmo), ZX_CACHE_POLICY_UNCACHED_DEVICE, &bars_[kLegacyBar]);
    ZX_ASSERT_MSG(status == ZX_OK, "Mapping BAR %u failed: %s", kLegacyBar,
                  zx_status_get_string(status));
  }

  // Even in the fake device we have to deal with registers being in different
  // places depending on whether MSI has been enabled or not.
  uint16_t LegacyDeviceCfgOffset() {
    return (fake_pci().GetIrqMode() == PCI_INTERRUPT_MODE_MSI_X) ? VIRTIO_PCI_CONFIG_OFFSET_MSIX
                                                                 : VIRTIO_PCI_CONFIG_OFFSET_NOMSIX;
  }

  void SetUpLegacyQueue() { bars_[kLegacyBar]->Write(kQueueSize, VIRTIO_PCI_QUEUE_SIZE); }

 private:
  std::array<std::optional<fdf::MmioBuffer>, PCI_MAX_BAR_REGS> bars_;
  pci::FakePciProtocol fake_pci_;
};

class TestVirtioDevice;
using DeviceType = ddk::Device<TestVirtioDevice, ddk::Unbindable>;
class TestVirtioDevice : public virtio::Device, public DeviceType {
 public:
  static const uint16_t kVirtqueueSize = 1u;

  explicit TestVirtioDevice(zx_device_t* bus_device, zx::bti bti,
                            std::unique_ptr<virtio::Backend> backend)
      : virtio::Device(bus_device, std::move(bti), std::move(backend)), DeviceType(bus_device) {}

  zx_status_t Init() final {
    // Initialize the first virtqueue.
    zx_status_t status = ring_.Init(0);
    if (status != ZX_OK) {
      return status;
    }
    return DdkAdd(tag());
  }
  void IrqRingUpdate() final {}
  void IrqConfigChange() final {}
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
  SetUpModernCapabilities();
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
            virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
}

TEST_F(VirtioTests, LegacyInterruptBindSuccess) {
  SetUpProtocol();
  SetUpModernCapabilities();
  SetUpModernBars();
  SetUpModernQueue();
  fake_pci().AddLegacyInterrupt();

  ASSERT_OK(virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
  CleanUp();
}

TEST_F(VirtioTests, FailureOneMsixBind) {
  fake_pci().AddMsixInterrupt();
  SetUpProtocol();
  SetUpModernCapabilities();
  SetUpModernBars();
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
            virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
}

TEST_F(VirtioTests, TwoMsixBindSuccess) {
  SetUpProtocol();
  SetUpModernCapabilities();
  SetUpModernBars();
  SetUpModernQueue();
  SetUpModernMsiX();

  // With everything set up this should succeed.
  ASSERT_OK(virtio::CreateAndBind<TestVirtioDevice>(nullptr, fake_ddk::kFakeParent));
  CleanUp();
}

// Ensure that the Legacy interface looks for IO Bar 0 and succeeds up until it
// tries to make IO writes using in/out instructions.
TEST_F(VirtioTests, DISABLED_LegacyIoBackendError) {
  fake_pci().AddLegacyInterrupt();
  SetUpProtocol();
  SetUpLegacyBar();
  SetUpLegacyQueue();
  auto backend_result = virtio::GetBtiAndBackend(fake_ddk::kFakeParent);
  ASSERT_OK(backend_result.status_value());
  // This should fail on x64 because of failure to access IO ports.
#ifdef __x86_64__
  TestVirtioDevice device(fake_ddk::kFakeParent, std::move(backend_result->first),
                          std::move(backend_result->second));
  ASSERT_DEATH([&device] { device.Init(); });
#endif
}

TEST_F(VirtioTests, LegacyIoBackendSuccess) {
  fake_pci().AddLegacyInterrupt();
  SetUpProtocol();
  SetUpLegacyBar();
  SetUpLegacyQueue();

  // With a manually crafted backend using the test interface it should succeed.
  ddk::PciProtocolClient pci(fake_ddk::kFakeParent);
  ASSERT_TRUE(pci.is_valid());
  pci_device_info_t info{};
  ASSERT_OK(pci.GetDeviceInfo(&info));
  zx::bti bti{};
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  // Feed the same vmo backing FakePci's BAR 0 into the interface so the view
  // from PCI, Virtio, and the test all align.
  TestLegacyIoInterface interface(bars()[kLegacyBar]->View(0));
  auto backend = std::make_unique<virtio::PciLegacyBackend>(pci, info, &interface);
  ASSERT_OK(backend->Bind());

  TestVirtioDevice device(fake_ddk::kFakeParent, std::move(bti), std::move(backend));
  ASSERT_OK(device.Init());
}

TEST_F(VirtioTests, LegacyMsiX) {
  fake_pci().AddMsixInterrupt();
  fake_pci().AddMsixInterrupt();
  SetUpProtocol();
  SetUpLegacyBar();
  SetUpLegacyQueue();

  ddk::PciProtocolClient pci(fake_ddk::kFakeParent);
  ASSERT_TRUE(pci.is_valid());
  pci_device_info_t info{};
  ASSERT_OK(pci.GetDeviceInfo(&info));
  zx::bti bti{};
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  TestLegacyIoInterface interface(bars()[kLegacyBar]->View(0));
  auto backend = std::make_unique<virtio::PciLegacyBackend>(pci, info, &interface);
  ASSERT_OK(backend->Bind());

  TestVirtioDevice device(fake_ddk::kFakeParent, std::move(bti), std::move(backend));
  ASSERT_OK(device.Init());

  // Verify MSI-X state
  ASSERT_EQ(fake_pci().GetIrqMode(), PCI_INTERRUPT_MODE_MSI_X);
  uint16_t value{};
  value = bars()[kLegacyBar]->Read16(VIRTIO_PCI_MSI_CONFIG_VECTOR);
  ASSERT_EQ(value, virtio::PciBackend::kMsiConfigVector);
  value = bars()[kLegacyBar]->Read16(VIRTIO_PCI_MSI_QUEUE_VECTOR);
  ASSERT_EQ(value, virtio::PciBackend::kMsiQueueVector);
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
