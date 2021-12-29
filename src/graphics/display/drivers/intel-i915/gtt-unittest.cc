// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/gtt.h"

#include <lib/fake-bti/bti.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/zircon-internal/align.h>

#include <gtest/gtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"
#include "src/graphics/display/drivers/intel-i915/registers.h"

namespace {

constexpr size_t kPageSize = PAGE_SIZE;

// Initialize the GTT to the smallest allowed size (which is 2MB with the |gtt_size| bits of the
// graphics control register set to 0x01.
constexpr size_t kTableSize = (1 << 21);
void Configure2MbGtt(pci::FakePciProtocol* pci) {
  pci->PciConfigWrite16(registers::GmchGfxControl::kAddr, 0x40);
}

ddk::MmioBuffer MakeMmioBuffer(uint8_t* buffer, size_t size) {
  return ddk::MmioBuffer({
      .vaddr = FakeMmioPtr(buffer),
      .offset = 0,
      .size = size,
      .vmo = ZX_HANDLE_INVALID,
  });
}

TEST(GttTest, InitWithZeroSizeGtt) {
  pci::FakePciProtocol pci;
  uint8_t buffer = 0;
  ddk::MmioBuffer mmio = MakeMmioBuffer(&buffer, 0);

  i915::Gtt gtt;
  EXPECT_EQ(ZX_ERR_INTERNAL, gtt.Init(&pci.get_protocol(), std::move(mmio), 0));

  // No MMIO writes should have occurred.
  EXPECT_EQ(0, buffer);
}

TEST(GttTest, InitGtt) {
  pci::FakePciProtocol pci;
  Configure2MbGtt(&pci);

  auto buffer = std::make_unique<uint8_t[]>(kTableSize);
  memset(buffer.get(), 0, kTableSize);
  ddk::MmioBuffer mmio = MakeMmioBuffer(buffer.get(), kTableSize);

  i915::Gtt gtt;
  EXPECT_EQ(ZX_OK, gtt.Init(&pci.get_protocol(), std::move(mmio), 0));

  // The table should contain 2MB / sizeof(uint64_t) 64-bit entries that map to the fake scratch
  // buffer. The "+ 1" marks bit 0 as 1 which denotes that's the page is present.
  uint64_t kBusPhysicalAddr = FAKE_BTI_PHYS_ADDR | 1;
  for (unsigned i = 0; i < kTableSize / sizeof(uint64_t); i++) {
    uint64_t addr = reinterpret_cast<uint64_t*>(buffer.get())[i];
    ASSERT_EQ(kBusPhysicalAddr, addr);
  }

  // Allocated GTT regions should start from base 0.
  std::unique_ptr<i915::GttRegion> region;
  EXPECT_EQ(ZX_OK, gtt.AllocRegion(kPageSize, kPageSize, &region));
  ASSERT_TRUE(region != nullptr);
  EXPECT_EQ(0u, region->base());
  EXPECT_EQ(kPageSize, region->size());
}

TEST(GttTest, InitGttWithFramebufferOffset) {
  pci::FakePciProtocol pci;
  Configure2MbGtt(&pci);

  // Treat the first 1024 bytes as the bootloader framebuffer region and initialize it to garbage.
  constexpr size_t kFbOffset = 1024;
  constexpr size_t kFbPages = ZX_ROUNDUP(kFbOffset, kPageSize) / kPageSize;
  constexpr uint8_t kJunk = 0xFF;
  auto buffer = std::make_unique<uint8_t[]>(kTableSize);
  memset(buffer.get(), kJunk, kTableSize);
  ddk::MmioBuffer mmio = MakeMmioBuffer(buffer.get(), kTableSize);

  i915::Gtt gtt;
  EXPECT_EQ(ZX_OK, gtt.Init(&pci.get_protocol(), std::move(mmio), kFbOffset));

  // The first page-aligned region of addresses should remain unmodified.
  for (unsigned i = 0; i < kFbPages; i++) {
    ASSERT_EQ(kJunk, buffer[i]);
  }

  // The table should contain 2MB / sizeof(uint64_t) 64-bit entries that map to the fake scratch
  // buffer. The "+ 1" marks bit 0 as 1 which denotes that's the page is present.
  uint64_t kBusPhysicalAddr = FAKE_BTI_PHYS_ADDR | 1;
  for (unsigned i = kFbPages; i < kTableSize / sizeof(uint64_t); i++) {
    uint64_t addr = reinterpret_cast<uint64_t*>(buffer.get())[i];
    ASSERT_EQ(kBusPhysicalAddr, addr);
  }

  // The first allocated GTT regions should exclude the framebuffer pages.
  std::unique_ptr<i915::GttRegion> region;
  EXPECT_EQ(ZX_OK, gtt.AllocRegion(kPageSize, kPageSize, &region));
  ASSERT_TRUE(region != nullptr);
  EXPECT_EQ(kFbPages * kPageSize, region->base());
  EXPECT_EQ(kPageSize, region->size());
}

TEST(GttTest, SetupForMexec) {
  pci::FakePciProtocol pci;
  Configure2MbGtt(&pci);
  auto buffer = std::make_unique<uint8_t[]>(kTableSize);
  ddk::MmioBuffer mmio = MakeMmioBuffer(buffer.get(), kTableSize);

  i915::Gtt gtt;
  EXPECT_EQ(ZX_OK, gtt.Init(&pci.get_protocol(), std::move(mmio), 0));

  // Assign an arbitrary page-aligned number as the stolen framebuffer address.
  constexpr uintptr_t kStolenFbMemory = kPageSize * 2;
  constexpr uint32_t kFbPages = ZX_ROUNDUP(1024, kPageSize) / kPageSize;
  gtt.SetupForMexec(kStolenFbMemory, kFbPages);

  for (unsigned i = 0; i < kFbPages; i++) {
    uint64_t addr = reinterpret_cast<uint64_t*>(buffer.get())[i];
    ASSERT_EQ(kStolenFbMemory | 0x01, addr);
  }

  // The mapping for the remaining pages should remain untouched.
  uint64_t kBusPhysicalAddr = FAKE_BTI_PHYS_ADDR + 1;
  for (unsigned i = kFbPages; i < kTableSize / sizeof(uint64_t); i++) {
    uint64_t addr = reinterpret_cast<uint64_t*>(buffer.get())[i];
    ASSERT_EQ(kBusPhysicalAddr, addr);
  }
}

}  // namespace
