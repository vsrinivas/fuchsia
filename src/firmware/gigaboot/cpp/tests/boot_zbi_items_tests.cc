// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/abr/abr.h>
#include <lib/zbitl/view.h>
#include <zircon/limits.h>

#include <efi/types.h>
#include <gtest/gtest.h>

#include "boot_zbi_items.h"
#include "gpt.h"
#include "mock_boot_service.h"
#include "utils.h"

namespace gigaboot {
namespace {

std::vector<zbitl::ByteView> FindItems(const void *zbi, uint32_t type) {
  std::vector<zbitl::ByteView> ret;
  zbitl::View<zbitl::ByteView> view{
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t *>(zbi))};
  for (auto [header, payload] : view) {
    if (header->type == type) {
      ret.push_back(payload);
    }
  }

  ZX_ASSERT(view.take_error().is_ok());
  return ret;
}

class BootZbiItemTest : public testing::Test {
 public:
  BootZbiItemTest() : image_device_({"path-A", "path-B", "path-C", "image"}) {
    stub_service_.AddDevice(&image_device_);
  }

  auto SetupEfiGlobalState() { return gigaboot::SetupEfiGlobalState(stub_service_, image_device_); }

  MockStubService &stub_service() { return stub_service_; }

 private:
  MockStubService stub_service_;
  Device image_device_;
};

TEST_F(BootZbiItemTest, AddMemoryRanges) {
  auto cleanup = SetupEfiGlobalState();

  std::vector<uint8_t> buffer(1024, 0);
  ASSERT_EQ(zbi_init(buffer.data(), buffer.size()), ZBI_RESULT_OK);

  // Don't care actual values. Choose any for test purpose.
  std::vector<efi_memory_descriptor> memory_map = {
      {
          .Type = EfiReservedMemoryType,
          .Padding = 0,
          .PhysicalStart = 0x0,
          .VirtualStart = 0x100000,
          .NumberOfPages = 0x10,
          .Attribute = EFI_MEMORY_UC,
      },
      {
          .Type = EfiLoaderCode,
          .Padding = 0,
          .PhysicalStart = 0x1000,
          .VirtualStart = 0x200000,
          .NumberOfPages = 0x10,
          .Attribute = EFI_MEMORY_UC,
      },
  };

  stub_service().SetMemoryMap(memory_map);

  ASSERT_TRUE(AddGigabootZbiItems(reinterpret_cast<zbi_header_t *>(buffer.data()), buffer.size(),
                                  kAbrSlotIndexA));

  std::vector<zbitl::ByteView> items = FindItems(buffer.data(), ZBI_TYPE_MEM_CONFIG);
  EXPECT_EQ(items.size(), 1ULL);

  cpp20::span<const zbi_mem_range_t> zbi_mem_ranges = {
      reinterpret_cast<const zbi_mem_range_t *>(items[0].data()),
      items[0].size() / sizeof(zbi_mem_range_t)};
  ASSERT_EQ(zbi_mem_ranges.size(), 2ULL);

  // Make sure that we added the expected items.
  EXPECT_EQ(zbi_mem_ranges[0].paddr, 0x0ULL);
  EXPECT_EQ(zbi_mem_ranges[0].length, 0x10 * ZX_PAGE_SIZE);
  EXPECT_EQ(zbi_mem_ranges[0].type, EfiToZbiMemRangeType(EfiReservedMemoryType));

  EXPECT_EQ(zbi_mem_ranges[1].paddr, 0x1000ULL);
  EXPECT_EQ(zbi_mem_ranges[1].length, 0x10 * ZX_PAGE_SIZE);
  EXPECT_EQ(zbi_mem_ranges[1].type, EfiToZbiMemRangeType(EfiLoaderCode));
}

TEST_F(BootZbiItemTest, AppendAbrSlotA) {
  auto cleanup = SetupEfiGlobalState();

  std::vector<uint8_t> buffer(1024, 0);
  ASSERT_EQ(zbi_init(buffer.data(), buffer.size()), ZBI_RESULT_OK);

  ASSERT_TRUE(AddGigabootZbiItems(reinterpret_cast<zbi_header_t *>(buffer.data()), buffer.size(),
                                  kAbrSlotIndexA));

  std::vector<zbitl::ByteView> items = FindItems(buffer.data(), ZBI_TYPE_CMDLINE);
  EXPECT_EQ(items.size(), 1ULL);

  ASSERT_EQ(std::string_view(reinterpret_cast<const char *>(items[0].data())),
            "zvb.current_slot=_a");
}

TEST_F(BootZbiItemTest, AppendAbrSlotB) {
  auto cleanup = SetupEfiGlobalState();

  std::vector<uint8_t> buffer(1024, 0);
  ASSERT_EQ(zbi_init(buffer.data(), buffer.size()), ZBI_RESULT_OK);

  ASSERT_TRUE(AddGigabootZbiItems(reinterpret_cast<zbi_header_t *>(buffer.data()), buffer.size(),
                                  kAbrSlotIndexB));

  std::vector<zbitl::ByteView> items = FindItems(buffer.data(), ZBI_TYPE_CMDLINE);
  EXPECT_EQ(items.size(), 1ULL);

  ASSERT_EQ(std::string_view(reinterpret_cast<const char *>(items[0].data())),
            "zvb.current_slot=_b");
}

}  // namespace

}  // namespace gigaboot
