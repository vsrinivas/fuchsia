// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/device-protocol/pci.h>
#include <lib/fit/function.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/zx/bti.h>
#include <lib/zx/object.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/syscalls/pci.h>
#include <zircon/system/public/zircon/syscalls.h>

#include <zxtest/zxtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"

class FakePciProtocolTests : public zxtest::Test {
 protected:
  void SetUp() final {
    fake_pci_.Reset();

    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_pci::Device>();
    ASSERT_OK(endpoints.status_value());

    fidl::BindServer(loop_.dispatcher(),
                     fidl::ServerEnd<fuchsia_hardware_pci::Device>(endpoints->server.TakeChannel()),
                     &fake_pci_);

    pci_ = ddk::Pci(std::move(endpoints->client));
    ASSERT_TRUE(pci_.is_valid());

    loop_.StartThread("pci-fidl-server-thread");
  }
  pci::FakePciProtocol& fake_pci() { return fake_pci_; }
  ddk::Pci& pci() { return pci_; }

  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};

 private:
  pci::FakePciProtocol fake_pci_;
  ddk::Pci pci_;
};

TEST_F(FakePciProtocolTests, CreateBar) {
  zx::vmo vmo;
  size_t size = 8193;
  ASSERT_OK(zx::vmo::create(size, 0, &vmo));

  pci::RunAsync(loop_, [&] { fake_pci().CreateBar(0, size, true); });

  fidl::Arena arena;
  fuchsia_hardware_pci::wire::Bar bar;
  pci().GetBar(arena, 0, &bar);
  EXPECT_EQ(size, bar.size);
}

TEST_F(FakePciProtocolTests, ResetDevice) {
  uint32_t reset_cnt = 0;
  pci::RunAsync(loop_, [&] { ASSERT_EQ(reset_cnt++, fake_pci().GetResetCount()); });
  ASSERT_OK(pci().ResetDevice());
  pci::RunAsync(loop_, [&] { ASSERT_EQ(reset_cnt++, fake_pci().GetResetCount()); });
  ASSERT_OK(pci().ResetDevice());
  pci::RunAsync(loop_, [&] { ASSERT_EQ(reset_cnt++, fake_pci().GetResetCount()); });
}

TEST_F(FakePciProtocolTests, GetBti) {
  zx::bti bti{};

  ASSERT_OK(pci().GetBti(0, &bti));
  zx_info_bti_t info;
  // Verify it's a BTI at least.
  ASSERT_OK(bti.get_info(ZX_INFO_BTI, &info, sizeof(info), /*actual_count=*/nullptr,
                         /*avail_count=*/nullptr));
}

TEST_F(FakePciProtocolTests, SetBusMastering) {
  // If enable has never been called there should be no value.
  pci::RunAsync(loop_, [&] { ASSERT_FALSE(fake_pci().GetBusMasterEnabled().has_value()); });

  ASSERT_OK(pci().SetBusMastering(true));
  pci::RunAsync(loop_, [&] { ASSERT_TRUE(fake_pci().GetBusMasterEnabled().value()); });

  ASSERT_OK(pci().SetBusMastering(false));
  pci::RunAsync(loop_, [&] { ASSERT_FALSE(fake_pci().GetBusMasterEnabled().value()); });
}

TEST_F(FakePciProtocolTests, GetDeviceInfo) {
  fuchsia_hardware_pci::wire::DeviceInfo actual{};
  fuchsia_hardware_pci::wire::DeviceInfo zeroed{};
  ASSERT_OK(pci().GetDeviceInfo(&actual));
  ASSERT_EQ(0, memcmp(&zeroed, &actual, sizeof(zeroed)));

  fuchsia_hardware_pci::wire::DeviceInfo expected = {
      .vendor_id = 0x1,
      .device_id = 0x2,

      .base_class = 0x3,
      .sub_class = 0x4,
      .program_interface = 0x5,
      .revision_id = 0x6,

      .bus_id = 0x7,
      .dev_id = 0x8,
      .func_id = 0x9,
  };

  pci::RunAsync(loop_, [&] { fake_pci().SetDeviceInfo(expected); });
  ASSERT_OK(pci().GetDeviceInfo(&actual));
  ASSERT_EQ(0, memcmp(&expected, &actual, sizeof(expected)));

  // Did we update the config header to match the device structure?
  uint8_t val8;
  uint16_t val16;
  ASSERT_OK(pci().ReadConfig16(PCI_CONFIG_VENDOR_ID, &val16));
  ASSERT_EQ(expected.vendor_id, val16);
  ASSERT_OK(pci().ReadConfig16(PCI_CONFIG_DEVICE_ID, &val16));
  ASSERT_EQ(expected.device_id, val16);
  ASSERT_OK(pci().ReadConfig8(PCI_CONFIG_REVISION_ID, &val8));
  ASSERT_EQ(expected.revision_id, val8);
  ASSERT_OK(pci().ReadConfig8(PCI_CONFIG_CLASS_CODE_BASE, &val8));
  ASSERT_EQ(expected.base_class, val8);
  ASSERT_OK(pci().ReadConfig8(PCI_CONFIG_CLASS_CODE_SUB, &val8));
  ASSERT_EQ(expected.sub_class, val8);
  ASSERT_OK(pci().ReadConfig8(PCI_CONFIG_CLASS_CODE_INTR, &val8));
  ASSERT_EQ(expected.program_interface, val8);
}

TEST_F(FakePciProtocolTests, GetInterruptModes) {
  fuchsia_hardware_pci::wire::InterruptModes modes{};
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(0, modes.has_legacy);
  ASSERT_EQ(0, modes.msi_count);
  ASSERT_EQ(0, modes.msix_count);

  pci::RunAsync(loop_, [&] { fake_pci().AddLegacyInterrupt(); });
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(1, modes.has_legacy);

  // MSI supports interrupt configuration via powers of two, so ensure that we
  // round down if not enough have been added.
  pci::RunAsync(loop_, [&] { fake_pci().AddMsiInterrupt(); });
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(1, modes.msi_count);
  pci::RunAsync(loop_, [&] { fake_pci().AddMsiInterrupt(); });
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(2, modes.msi_count);
  pci::RunAsync(loop_, [&] { fake_pci().AddMsiInterrupt(); });
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(2, modes.msi_count);
  pci::RunAsync(loop_, [&] { fake_pci().AddMsiInterrupt(); });
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(4, modes.msi_count);

  // MSI-X doesn't care about alignment, so any value should work.
  pci::RunAsync(loop_, [&] { fake_pci().AddMsixInterrupt(); });
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(1, modes.msix_count);
  pci::RunAsync(loop_, [&] { fake_pci().AddMsixInterrupt(); });
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(2, modes.msix_count);
  pci::RunAsync(loop_, [&] { fake_pci().AddMsixInterrupt(); });
  pci().GetInterruptModes(&modes);
  ASSERT_EQ(3, modes.msix_count);
}

TEST_F(FakePciProtocolTests, SetInterruptMode) {
  pci::RunAsync(loop_, [&] {
    fake_pci().AddLegacyInterrupt();
    fake_pci().AddMsiInterrupt();
    fake_pci().AddMsiInterrupt();
    fake_pci().AddMsiInterrupt();
    fake_pci().AddMsiInterrupt();
    fake_pci().AddMsixInterrupt();
    fake_pci().AddMsixInterrupt();
  });

  fuchsia_hardware_pci::InterruptMode mode = fuchsia_hardware_pci::InterruptMode::kLegacy;
  ASSERT_OK(pci().SetInterruptMode(mode, 1));
  pci::RunAsync(loop_, [&] {
    ASSERT_EQ(1, fake_pci().GetIrqCount());
    ASSERT_EQ(fidl::ToUnderlying(mode), fake_pci().GetIrqMode());
  });
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().SetInterruptMode(mode, 2));

  mode = fuchsia_hardware_pci::InterruptMode::kMsi;
  ASSERT_OK(pci().SetInterruptMode(mode, 1));
  pci::RunAsync(loop_, [&] {
    ASSERT_EQ(1, fake_pci().GetIrqCount());
    ASSERT_EQ(fidl::ToUnderlying(mode), fake_pci().GetIrqMode());
  });

  ASSERT_OK(pci().SetInterruptMode(mode, 2));
  pci::RunAsync(loop_, [&] {
    ASSERT_EQ(2, fake_pci().GetIrqCount());
    ASSERT_EQ(fidl::ToUnderlying(mode), fake_pci().GetIrqMode());
  });

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().SetInterruptMode(mode, 3));
  pci::RunAsync(loop_, [&] {
    ASSERT_EQ(2, fake_pci().GetIrqCount());
    ASSERT_EQ(fidl::ToUnderlying(mode), fake_pci().GetIrqMode());
  });

  ASSERT_OK(pci().SetInterruptMode(mode, 4));
  pci::RunAsync(loop_, [&] {
    ASSERT_EQ(4, fake_pci().GetIrqCount());
    ASSERT_EQ(fidl::ToUnderlying(mode), fake_pci().GetIrqMode());
  });
}

namespace {
// When interrupts are added to the fake a borrowed copy of the interrupt is
// returned for comparison by tests later. Its koid should match the koid of the
// duplicated handle returned by MapInterrupt.
template <typename T>
bool MatchKoids(zx_handle_t first, const zx::object<T>& second) {
  zx_info_handle_basic finfo{}, sinfo{};
  ZX_ASSERT(zx_object_get_info(first, ZX_INFO_HANDLE_BASIC, &finfo, sizeof(finfo), nullptr,
                               nullptr) == ZX_OK);
  ZX_ASSERT(second.get_info(ZX_INFO_HANDLE_BASIC, &sinfo, sizeof(sinfo), nullptr, nullptr) ==
            ZX_OK);

  return finfo.koid == sinfo.koid;
}
}  // namespace

TEST_F(FakePciProtocolTests, MapInterrupt) {
  // One notable difference between this fake and the real PCI protocol is that
  // it is an error to call SetInterruptMode and switch modes if an existing MSI is
  // mapped still. In the fake though, it's fine to do so. Switching IRQ modes
  // is not something drivers do in practice, so it's fine if they encounter
  // ZX_ERR_BAD_STATE at runtime if documentation details it.
  zx_handle_t legacy, msi0, msi1, msix0, msix1, msix2;
  pci::RunAsync(loop_, [&] {
    legacy = fake_pci().AddLegacyInterrupt().get();
    msi0 = fake_pci().AddMsiInterrupt().get();
    msi1 = fake_pci().AddMsiInterrupt().get();
    msix0 = fake_pci().AddMsixInterrupt().get();
    msix1 = fake_pci().AddMsixInterrupt().get();
    msix2 = fake_pci().AddMsixInterrupt().get();
  });

  zx::interrupt interrupt{};
  uint32_t irq_cnt = 1;
  ASSERT_OK(pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kLegacy, irq_cnt));
  ASSERT_OK(pci().MapInterrupt(0, &interrupt));
  ASSERT_TRUE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().MapInterrupt(irq_cnt, &interrupt));
  interrupt.reset();

  ASSERT_OK(pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kLegacyNoack, irq_cnt));
  ASSERT_OK(pci().MapInterrupt(0, &interrupt));
  ASSERT_TRUE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().MapInterrupt(irq_cnt, &interrupt));
  interrupt.reset();

  irq_cnt = 2;
  ASSERT_OK(pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kMsi, irq_cnt));
  ASSERT_OK(pci().MapInterrupt(0, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_TRUE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().MapInterrupt(irq_cnt, &interrupt));
  interrupt.reset();

  ASSERT_OK(pci().MapInterrupt(1, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_TRUE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  interrupt.reset();

  irq_cnt = 3;
  ASSERT_OK(pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kMsiX, irq_cnt));
  ASSERT_OK(pci().MapInterrupt(0, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_TRUE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  interrupt.reset();

  ASSERT_OK(pci().MapInterrupt(1, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_TRUE(MatchKoids(msix1, interrupt));
  ASSERT_FALSE(MatchKoids(msix2, interrupt));
  interrupt.reset();

  ASSERT_OK(pci().MapInterrupt(2, &interrupt));
  ASSERT_FALSE(MatchKoids(legacy, interrupt));
  ASSERT_FALSE(MatchKoids(msi0, interrupt));
  ASSERT_FALSE(MatchKoids(msi1, interrupt));
  ASSERT_FALSE(MatchKoids(msix0, interrupt));
  ASSERT_FALSE(MatchKoids(msix1, interrupt));
  ASSERT_TRUE(MatchKoids(msix2, interrupt));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().MapInterrupt(irq_cnt, &interrupt));
}

TEST_F(FakePciProtocolTests, VerifyAllocatedMsis) {
  pci::RunAsync(loop_, [&] {
    fake_pci().AddLegacyInterrupt();
    fake_pci().AddMsiInterrupt();
    fake_pci().AddMsiInterrupt();
    fake_pci().AddMsixInterrupt();
  });

  zx::interrupt zero, one;
  ASSERT_OK(pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kMsi, 2));
  ASSERT_OK(pci().MapInterrupt(0, &zero));
  ASSERT_OK(pci().MapInterrupt(1, &one));
  // Changing to other IRQ modes should be blocked because IRQ handles are outstanding.
  ASSERT_EQ(ZX_ERR_BAD_STATE,
            pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kLegacy, 1));
  ASSERT_EQ(ZX_ERR_BAD_STATE,
            pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kLegacyNoack, 1));
  ASSERT_EQ(ZX_ERR_BAD_STATE,
            pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kMsiX, 1));
  zero.reset();
  one.reset();
  // Now transitioning should work.
  ASSERT_OK(pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kLegacy, 1));
  ASSERT_OK(pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kMsiX, 1));

  // Verify MSI-X works the same.
  ASSERT_OK(pci().MapInterrupt(0, &zero));
  ASSERT_EQ(ZX_ERR_BAD_STATE,
            pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kLegacy, 1));
  zero.reset();
  ASSERT_OK(pci().SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kLegacy, 1));
}

TEST_F(FakePciProtocolTests, ConfigRW) {
  zx::unowned_vmo config;
  pci::RunAsync(loop_, [&] { config = fake_pci().GetConfigVmo(); });

  // Verify the header space range. Reads can read the header [0, 63], but
  // writes cannot. All IO must fit within the config space [0, 255].
  uint8_t val8;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, pci().WriteConfig8(0, 0xFF));
  ASSERT_EQ(ZX_OK, pci().ReadConfig8(0, &val8));
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, pci().WriteConfig8(PCI_CONFIG_HEADER_SIZE - 1, 0xFF));
  ASSERT_EQ(ZX_OK, pci().ReadConfig8(PCI_CONFIG_HEADER_SIZE - 1, &val8));
  // The ensures we also verify that offset + read/write size is within bounds.
  uint32_t val32;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, pci().WriteConfig32(PCI_BASE_CONFIG_SIZE - 2, 0xFF));
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, pci().ReadConfig32(PCI_BASE_CONFIG_SIZE - 2, &val32));

  for (uint16_t off = PCI_CONFIG_HEADER_SIZE; off < PCI_BASE_CONFIG_SIZE; off++) {
    uint8_t val8;
    pci().WriteConfig8(off, off);
    pci().ReadConfig8(off, &val8);
    ASSERT_EQ(off, val8);
    ASSERT_OK(config->read(&val8, off, sizeof(val8)));
    ASSERT_EQ(off, val8);
  }

  for (uint16_t off = PCI_CONFIG_HEADER_SIZE; off < PCI_BASE_CONFIG_SIZE - 1; off++) {
    uint16_t val16;
    pci().WriteConfig16(off, off);
    pci().ReadConfig16(off, &val16);
    ASSERT_EQ(off, val16);
    ASSERT_OK(config->read(&val16, off, sizeof(val16)));
    ASSERT_EQ(off, val16);
  }

  for (uint16_t off = PCI_CONFIG_HEADER_SIZE; off < PCI_BASE_CONFIG_SIZE - 3; off++) {
    uint32_t val32;
    pci().WriteConfig32(off, off);
    pci().ReadConfig32(off, &val32);
    ASSERT_EQ(off, val32);
    ASSERT_OK(config->read(&val32, off, sizeof(val32)));
    ASSERT_EQ(off, val32);
  }
}

TEST_F(FakePciProtocolTests, GetBar) {
  fuchsia_hardware_pci::wire::Bar bar{};
  fidl::Arena arena;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, pci().GetBar(arena, 0, &bar));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, pci().GetBar(arena, 6, &bar));

  uint32_t bar_id = 3;
  size_t size = 256;
  pci::RunAsync(loop_,
                [&] { ASSERT_NO_DEATH([&]() { fake_pci().CreateBar(bar_id, size, true); }); });
  // Verify that the VMO we got back via the protocol method matches the setup
  // and that the other fields are correct.
  ASSERT_OK(pci().GetBar(arena, bar_id, &bar));
  zx::vmo proto = std::move(bar.result.vmo());
  zx_handle_t borrowed;
  pci::RunAsync(loop_, [&] { borrowed = fake_pci().GetBar(bar_id).get(); });
  ASSERT_TRUE(MatchKoids(borrowed, proto));
  ASSERT_EQ(bar_id, bar.bar_id);
  ASSERT_EQ(size, bar.size);
}

TEST_F(FakePciProtocolTests, BarTypes) {
  size_t page_size = zx_system_get_page_size();
  pci::RunAsync(loop_, [&] {
    fake_pci().CreateBar(0, page_size, true);
    fake_pci().CreateBar(1, page_size, false);
  });

  fidl::Arena arena;
  fuchsia_hardware_pci::wire::Bar bar;
  ASSERT_OK(pci().GetBar(arena, 0, &bar));
  ASSERT_TRUE(bar.result.is_vmo());
  ASSERT_OK(pci().GetBar(arena, 1, &bar));
  ASSERT_TRUE(bar.result.is_io());
}

TEST_F(FakePciProtocolTests, MapMmio) {
  const uint32_t bar_id = 0;
  const uint64_t bar_size = 256;
  zx_handle_t borrowed;
  pci::RunAsync(loop_, [&] {
    fake_pci().CreateBar(bar_id, bar_size, true);
    borrowed = fake_pci().GetBar(bar_id).get();
  });

  // Ensure that our fake implementation / backend for the BAR methods still works with
  // the MapMmio helper method added to device-protocol.
  std::optional<fdf::MmioBuffer> mmio = std::nullopt;
  ASSERT_OK(pci().MapMmio(bar_id, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio));
  ASSERT_TRUE(MatchKoids(borrowed, *mmio->get_vmo()));
}

TEST_F(FakePciProtocolTests, Capabilities) {
  pci::RunAsync(loop_, [&] {
    // Try invalid capabilities.
    ASSERT_DEATH([&]() { fake_pci().AddCapability(0, PCI_CONFIG_HEADER_SIZE, 16); });
    ASSERT_DEATH([&]() {
      fake_pci().AddCapability(PCI_CAPABILITY_ID_FLATTENING_PORTAL_BRIDGE + 1,
                               PCI_CONFIG_HEADER_SIZE, 16);
    });

    // Try invalid locations.
    ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(PCI_CONFIG_HEADER_SIZE - 16, 32); });
    ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(PCI_BASE_CONFIG_SIZE - 16, 32); });

    // Overlap tests.
    ASSERT_NO_DEATH([&]() { fake_pci().AddVendorCapability(0xB0, 16); });
    ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(0xB0 + 8, 16); });
    ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(0xB0 - 8, 16); });
    ASSERT_DEATH([&]() { fake_pci().AddVendorCapability(0xB0, 32); });
  });
}

TEST_F(FakePciProtocolTests, PciGetFirstAndNextCapability) {
  zx::unowned_vmo config;
  pci::RunAsync(loop_, [&] {
    config = fake_pci().GetConfigVmo();
    // The first capability should set up the capabilities pointer.
    fake_pci().AddVendorCapability(0x50, 6);
  });

  uint8_t offset1 = 0;
  ASSERT_OK(pci().GetFirstCapability(fuchsia_hardware_pci::CapabilityId::kVendor, &offset1));
  uint8_t val;
  config->read(&val, PCI_CONFIG_CAPABILITIES_PTR, sizeof(val));
  ASSERT_EQ(0x50, val);
  config->read(&val, offset1, sizeof(val));
  ASSERT_EQ(fidl::ToUnderlying(fuchsia_hardware_pci::CapabilityId::kVendor), val);
  config->read(&val, offset1 + 2, sizeof(val));
  ASSERT_EQ(6, val);

  // After adding the new capability we need to check that the previous next pointer was set up.
  pci::RunAsync(loop_, [&] { fake_pci().AddVendorCapability(0x60, 8); });
  config->read(&val, 0x51, sizeof(val));
  ASSERT_EQ(val, 0x60);

  // Can we find sequential capabilities, or different IDs?
  uint8_t offset2 = 0;
  ASSERT_OK(
      pci().GetNextCapability(fuchsia_hardware_pci::CapabilityId::kVendor, offset1, &offset2));
  ASSERT_EQ(0x60, offset2);

  pci::RunAsync(loop_, [&] {
    fake_pci().AddPciExpressCapability(0x70);
    fake_pci().AddVendorCapability(0xB0, 16);
  });

  ASSERT_OK(pci().GetFirstCapability(fuchsia_hardware_pci::CapabilityId::kPciExpress, &offset1));
  ASSERT_EQ(0x70, offset1);

  ASSERT_OK(
      pci().GetNextCapability(fuchsia_hardware_pci::CapabilityId::kVendor, offset2, &offset1));
  ASSERT_EQ(0xB0, offset1);
}
