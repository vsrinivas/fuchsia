// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../mem_config.h"

#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/image.h>

#include <efi/boot-services.h>
#include <efi/runtime-services.h>
#include <gtest/gtest.h>

namespace {

// Append the given objects together as a series of bytes.
template <typename... T>
std::vector<std::byte> JoinBytes(const T&... object) {
  std::vector<std::byte> result;

  // Add the bytes from a single item to |result|.
  auto add_item = [&result](const auto& x) {
    zbitl::ByteView object_bytes = zbitl::AsBytes(x);
    result.insert(result.end(), object_bytes.begin(), object_bytes.end());
  };

  // Add each item.
  (add_item(object), ...);

  return result;
}

// Determine if two `zbi_mem_range_t` values are the same.
bool MemRangeEqual(const zbi_mem_range_t& a, const zbi_mem_range_t& b) {
  return std::tie(a.length, a.paddr, a.reserved, a.type) ==
         std::tie(b.length, b.paddr, b.reserved, b.type);
}

zbi_header_t ZbiContainerHeader(uint32_t size) { return ZBI_CONTAINER_HEADER(size); }

// Create a ZBI header for an item.
zbi_header_t ZbiItemHeader(uint32_t type, size_t size) {
  return {
      .type = type,
      .length = static_cast<uint32_t>(size),
      .flags = ZBI_FLAG_VERSION,
      .magic = ZBI_ITEM_MAGIC,
      .crc32 = ZBI_ITEM_NO_CRC32,
  };
}

TEST(ToMemRange, Efi) {
  constexpr efi_memory_descriptor efi{
      .Type = EfiConventionalMemory,
      .PhysicalStart = 0x1234'abcd'ffff'0000,
      .VirtualStart = 0xaaaa'aaaa'aaaa'aaaa,
      .NumberOfPages = 100,
      .Attribute = EFI_MEMORY_MORE_RELIABLE,
  };
  constexpr zbi_mem_range_t expected{
      .paddr = 0x1234'abcd'ffff'0000,
      .length = 409600,  // ZX_PAGE_SIZE * 100
      .type = ZBI_MEM_RANGE_RAM,
  };
  EXPECT_TRUE(MemRangeEqual(zbitl::internal::ToMemRange(efi), expected));
}

TEST(ToMemRange, EfiReservedMemory) {
  auto efi = efi_memory_descriptor{
      .Type = EfiMemoryMappedIO,
      .PhysicalStart = 0x0,
      .VirtualStart = 0x0,
      .NumberOfPages = 1,
      .Attribute = 0,
  };
  EXPECT_EQ(zbitl::internal::ToMemRange(efi).type, static_cast<uint32_t>(ZBI_MEM_RANGE_RESERVED));
}

TEST(ToMemRange, E820) {
  auto input = e820entry_t{
      .addr = 0x1234'abcd'ffff'0000,
      .size = 0x10'0000,
      .type = E820_RAM,
  };
  auto expected = zbi_mem_range_t{
      .paddr = 0x1234'abcd'ffff'0000,
      .length = 0x10'0000,
      .type = ZBI_MEM_RANGE_RAM,
  };
  EXPECT_TRUE(MemRangeEqual(zbitl::internal::ToMemRange(input), expected));
}

TEST(MemRangeIterator, DefaultContainer) {
  zbitl::MemRangeTable container;

  EXPECT_EQ(container.begin(), container.end());
  EXPECT_TRUE(container.take_error().is_ok());
}

TEST(MemRangeIterator, EmptyZbi) {
  zbi_header_t header = ZbiContainerHeader(0);
  zbitl::View<zbitl::ByteView> view(zbitl::AsBytes(header));
  zbitl::MemRangeTable container{view};

  // Expect nothing to be found.
  EXPECT_EQ(container.begin(), container.end());
  EXPECT_TRUE(container.take_error().is_ok());
}

TEST(MemRangeIterator, BadZbi) {
  zbi_header_t header = ZbiContainerHeader(0);
  header.crc32 = 0xffffffff;  // bad CRC.
  zbitl::View<zbitl::ByteView> view(zbitl::AsBytes(header));
  zbitl::MemRangeTable container{view};

  // Expect nothing to be found.
  EXPECT_EQ(container.begin(), container.end());

  // Expect an error.
  auto error = container.take_error();
  ASSERT_TRUE(error.is_error());
  EXPECT_EQ(error.error_value().zbi_error, "bad crc32 field in item without CRC");
}

TEST(MemRangeIterator, RequireErrorToBeCalled) {
  zbi_header_t header = ZbiContainerHeader(0);
  zbitl::View<zbitl::ByteView> view(zbitl::AsBytes(header));

  // Iterate through an empty item and then destroy it without calling Error().
  ASSERT_DEATH(
      {
        zbitl::MemRangeTable container{view};

        // Expect nothing to be found.
        EXPECT_EQ(container.begin(), container.end());

        // Don't call `take_error`: expect process death during object destruction.
      },
      "destroyed .* without check");
}

TEST(MemRangeIterator, NoErrorNeededAfterMove) {
  zbi_header_t header = ZbiContainerHeader(0);
  zbitl::View<zbitl::ByteView> view(zbitl::AsBytes(header));
  zbitl::MemRangeTable container{view};

  // Iterate through an empty item.
  container.begin();

  // Move the value, and check the error in its new location. We shouldn't
  // need to check the first any longer.
  zbitl::MemRangeTable new_container = std::move(container);
  EXPECT_TRUE(new_container.take_error().is_ok());
}

TEST(MemRangeIterator, EmptyPayload) {
  // Construct a ZBI with an empty EFI memory map.
  std::vector<std::byte> bytes = JoinBytes(ZbiContainerHeader(sizeof(zbi_header_t)),
                                           ZbiItemHeader(ZBI_TYPE_EFI_MEMORY_MAP, 0));
  zbitl::View view{zbitl::ByteView(bytes.data(), bytes.size())};
  zbitl::MemRangeTable container{view};

  // Expect nothing to be found.
  EXPECT_EQ(container.begin(), container.end());
  EXPECT_TRUE(container.take_error().is_ok());
}

TEST(MemRangeIterator, EfiItem) {
  // Construct a ZBI with a single payload consisting of EFI entries.
  std::vector<std::byte> data =
      JoinBytes(ZbiContainerHeader(sizeof(zbi_header_t) + sizeof(efi_memory_descriptor) * 2),
                ZbiItemHeader(ZBI_TYPE_EFI_MEMORY_MAP, sizeof(efi_memory_descriptor) * 2),
                efi_memory_descriptor{
                    .PhysicalStart = 0x1000,
                    .NumberOfPages = 1,
                },
                efi_memory_descriptor{
                    .PhysicalStart = 0x2000,
                    .NumberOfPages = 1,
                });
  zbitl::View view{zbitl::ByteView(data.data(), data.size())};

  // Ensure the entries are correct.
  zbitl::MemRangeTable container{view};
  std::vector<zbi_mem_range_t> ranges(container.begin(), container.end());
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_EQ(ranges[0].paddr, 0x1000u);
  EXPECT_EQ(ranges[1].paddr, 0x2000u);
  EXPECT_TRUE(container.take_error().is_ok());
}

TEST(MemRangeIterator, ZbiMemRangeItem) {
  // Construct a ZBI with a single payload consisting of zbi_mem_range_t entries.
  std::vector<std::byte> data =
      JoinBytes(ZbiContainerHeader(sizeof(zbi_header_t) + sizeof(zbi_mem_range_t) * 2),
                ZbiItemHeader(ZBI_TYPE_MEM_CONFIG, sizeof(zbi_mem_range_t) * 2),
                zbi_mem_range_t{
                    .paddr = 0x1000,
                    .length = 0x1000,
                },
                zbi_mem_range_t{
                    .paddr = 0x2000,
                    .length = 0x1000,
                });
  auto bv = zbitl::ByteView(data.data(), data.size());
  zbitl::View view{zbitl::ByteView(data.data(), data.size())};

  // Ensure the entries are correct.
  zbitl::MemRangeTable container{view};
  std::vector<zbi_mem_range_t> ranges(container.begin(), container.end());
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_EQ(ranges[0].paddr, 0x1000u);
  EXPECT_EQ(ranges[1].paddr, 0x2000u);
  EXPECT_TRUE(container.take_error().is_ok());
}

TEST(MemRangeIterator, E820Item) {
  // Construct a ZBI with a single payload consisting of e820entry_t entries.
  std::vector<std::byte> data =
      JoinBytes(ZbiContainerHeader(sizeof(zbi_header_t) + sizeof(e820entry_t) * 2),
                ZbiItemHeader(ZBI_TYPE_E820_TABLE, sizeof(e820entry_t) * 2),
                e820entry_t{
                    .addr = 0x1000,
                    .size = 0x1000,
                },
                e820entry_t{
                    .addr = 0x2000,
                    .size = 0x1000,
                });
  zbitl::View view{zbitl::ByteView(data.data(), data.size())};

  // Ensure the entries are correct.
  zbitl::MemRangeTable container{view};
  std::vector<zbi_mem_range_t> ranges(container.begin(), container.end());
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_EQ(ranges[0].paddr, 0x1000u);
  EXPECT_EQ(ranges[1].paddr, 0x2000u);
  EXPECT_TRUE(container.take_error().is_ok());
}

TEST(MemRangeIterator, MixedItems) {
  // Construct a ZBI with a mixed set of payloads.
  std::vector<std::byte> data =
      JoinBytes(ZbiContainerHeader(sizeof(zbi_header_t) * 3 + sizeof(e820entry_t) +
                                   sizeof(zbi_mem_range_t) + sizeof(efi_memory_descriptor) + 4),
                // E820
                ZbiItemHeader(ZBI_TYPE_E820_TABLE, sizeof(e820entry_t)),
                e820entry_t{
                    .addr = 0x1000,
                    .size = 0x1000,
                },
                // padding
                static_cast<uint32_t>(0u),
                // zbi_mem_range_t,
                ZbiItemHeader(ZBI_TYPE_MEM_CONFIG, sizeof(zbi_mem_range_t)),
                zbi_mem_range_t{
                    .paddr = 0x2000,
                    .length = 0x2000,
                },
                // EFI
                ZbiItemHeader(ZBI_TYPE_EFI_MEMORY_MAP, sizeof(efi_memory_descriptor)),
                efi_memory_descriptor{
                    .PhysicalStart = 0x3000,
                    .NumberOfPages = 3,
                });
  zbitl::View view{zbitl::ByteView(data.data(), data.size())};

  // Ensure the entries are correct.
  zbitl::MemRangeTable container{view};
  std::vector<zbi_mem_range_t> ranges(container.begin(), container.end());
  ASSERT_EQ(ranges.size(), 3u);
  EXPECT_EQ(ranges[0].paddr, 0x1000u);
  EXPECT_EQ(ranges[1].paddr, 0x2000u);
  EXPECT_EQ(ranges[2].paddr, 0x3000u);
  EXPECT_TRUE(container.take_error().is_ok());
}

TEST(MemRangeIterator, OtherItems) {
  // Construct a ZBI with non-memory payloads.
  std::vector<std::byte> data =
      JoinBytes(ZbiContainerHeader(sizeof(zbi_header_t) * 3 + sizeof(zbi_mem_range_t)),
                ZbiItemHeader(ZBI_TYPE_PLATFORM_ID, 0),
                // zbi_mem_range_t,
                ZbiItemHeader(ZBI_TYPE_MEM_CONFIG, sizeof(zbi_mem_range_t)),
                zbi_mem_range_t{
                    .paddr = 0x1000,
                    .length = 0x1000,
                },
                ZbiItemHeader(ZBI_TYPE_PLATFORM_ID, 0));
  zbitl::View view{zbitl::ByteView(data.data(), data.size())};

  // Ensure the entries are correct.
  zbitl::MemRangeTable container{view};
  std::vector<zbi_mem_range_t> ranges(container.begin(), container.end());
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_EQ(ranges[0].paddr, 0x1000u);
  EXPECT_TRUE(container.take_error().is_ok());
}

}  // namespace
