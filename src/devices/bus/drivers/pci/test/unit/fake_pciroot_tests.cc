// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/syscalls/resource.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/devices/bus/drivers/pci/test/fakes/fake_pciroot.h"

namespace pci {

class FakePcirootTests : public zxtest::Test {
 protected:
  void SetUp() final { pciroot_ = FakePciroot(0, 1); }
  auto& pciroot() { return pciroot_; }

 private:
  FakePciroot pciroot_;
};

TEST(FakePcirootTests, Constructor) {
  uint8_t bus_start = 0;
  uint8_t bus_end = 1;
  FakePciroot pciroot(bus_start, bus_end);
  ASSERT_EQ(bus_start, pciroot.bus_start());
  ASSERT_EQ(bus_end, pciroot.bus_end());
}

TEST_F(FakePcirootTests, GetBti) {
  zx::bti bti;
  ASSERT_OK(pciroot().PcirootGetBti(0, 0, &bti));
  zx_info_bti_t info{};
  ASSERT_OK(bti.get_info(ZX_INFO_BTI, &info, sizeof(info), nullptr, nullptr));
  pciroot().enable_get_bti(false);
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootGetBti(0, 0, &bti));
}

TEST_F(FakePcirootTests, GetPciPlatformInfo) {
  pci_platform_info_t info;
  ASSERT_OK(pciroot().PcirootGetPciPlatformInfo(&info));
  ASSERT_EQ(pciroot().bus_start(), info.start_bus_num);
  ASSERT_EQ(pciroot().bus_end(), info.end_bus_num);
  ASSERT_STREQ("fakroot", info.name);
  pciroot().enable_get_pci_platform_info(false);
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootGetPciPlatformInfo(&info));
}

TEST_F(FakePcirootTests, ConfigRead) {
  uint8_t u8;
  uint16_t u16;
  uint32_t u32;

  pci_bdf_t bdf = {0, 0, 0};
  ASSERT_OK(pciroot().PcirootConfigRead8(&bdf, 0, &u8));
  ASSERT_OK(pciroot().PcirootConfigRead16(&bdf, 0, &u16));
  ASSERT_OK(pciroot().PcirootConfigRead32(&bdf, 0, &u32));
  pciroot().enable_config_read(false);
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootConfigRead8(&bdf, 0, &u8));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootConfigRead16(&bdf, 0, &u16));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootConfigRead32(&bdf, 0, &u32));
}

TEST_F(FakePcirootTests, ConfigWrite) {
  pci_bdf_t bdf = {0, 0, 0};
  ASSERT_OK(pciroot().PcirootConfigWrite8(&bdf, 0, 0xA5));
  ASSERT_OK(pciroot().PcirootConfigWrite16(&bdf, 0, 0xA5A5));
  ASSERT_OK(pciroot().PcirootConfigWrite32(&bdf, 0, 0xA5A5A5A5));
  pciroot().enable_config_write(false);
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootConfigWrite8(&bdf, 0, 0xA5));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootConfigWrite16(&bdf, 0, 0xA5A5));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootConfigWrite32(&bdf, 0, 0xA5A5A5A5));
}

TEST_F(FakePcirootTests, DriverShouldProxyConfig) {
  ASSERT_FALSE(pciroot().PcirootDriverShouldProxyConfig());
  pciroot().enable_driver_should_proxy_config(true);
  ASSERT_TRUE(pciroot().PcirootDriverShouldProxyConfig());
}

TEST_F(FakePcirootTests, AllocateMsi) {
  zx::msi msi;
  uint32_t msi_count = 2;
  ASSERT_OK(pciroot().PcirootAllocateMsi(msi_count, false, &msi));
  zx_info_msi_t info{};
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(info.num_irq, msi_count);
  ASSERT_EQ(info.interrupt_count, 0);
  pciroot().enable_allocate_msi(false);
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, pciroot().PcirootAllocateMsi(msi_count, false, &msi));
}

TEST_F(FakePcirootTests, GetAddressSpace) {
  zx_paddr_t in_base = 0;
  uint64_t out_base;
  size_t size = 4096;
  zx::resource resource;
  zx_info_resource_t resource_info{};
  zx::eventpair eventpair;
  // High, Mmio
  ASSERT_OK(pciroot().PcirootGetAddressSpace(in_base, size, /*type=*/PCI_ADDRESS_SPACE_MEMORY,
                                             /*low=*/false, &out_base, &resource, &eventpair));
  ASSERT_OK(
      resource.get_info(ZX_INFO_RESOURCE, &resource_info, sizeof(resource_info), nullptr, nullptr));
  ASSERT_EQ(ZX_RSRC_KIND_MMIO, resource_info.kind);
  ASSERT_EQ(size, resource_info.size);
  ASSERT_EQ(FakePciroot::kDefaultHighMemoryAddress, resource_info.base);

  // Low, Mmio
  ASSERT_OK(pciroot().PcirootGetAddressSpace(in_base, size, /*type=*/PCI_ADDRESS_SPACE_MEMORY,
                                             /*low=*/true, &out_base, &resource, &eventpair));
  ASSERT_OK(
      resource.get_info(ZX_INFO_RESOURCE, &resource_info, sizeof(resource_info), nullptr, nullptr));
  ASSERT_EQ(ZX_RSRC_KIND_MMIO, resource_info.kind);
  ASSERT_EQ(size, resource_info.size);
  ASSERT_EQ(FakePciroot::kDefaultLowMemoryAddress, resource_info.base);

  // IO
  ASSERT_OK(pciroot().PcirootGetAddressSpace(in_base, size, /*type=*/PCI_ADDRESS_SPACE_IO,
                                             /*low=*/false, &out_base, &resource, &eventpair));
  ASSERT_OK(
      resource.get_info(ZX_INFO_RESOURCE, &resource_info, sizeof(resource_info), nullptr, nullptr));
  ASSERT_EQ(ZX_RSRC_KIND_IOPORT, resource_info.kind);
  ASSERT_EQ(size, resource_info.size);
  ASSERT_EQ(FakePciroot::kDefaultIoAddress, resource_info.base);

  // Check out_base == in_base
  in_base = 0xBEEE;
  ASSERT_OK(pciroot().PcirootGetAddressSpace(in_base, size, /*type=*/PCI_ADDRESS_SPACE_IO,
                                             /*low=*/false, &out_base, &resource, &eventpair));
  ASSERT_EQ(in_base, out_base);

  pciroot().enable_get_address_space(false);
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED,
                pciroot().PcirootGetAddressSpace(in_base, size, /*type=*/PCI_ADDRESS_SPACE_IO,
                                                 /*low=*/false, &out_base, &resource, &eventpair));
}

}  // namespace pci
