// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/ftl_image_internal.h"

#include <lib/fit/result.h>

#include <cstdint>
#include <iterator>
#include <limits>

#include <fbl/algorithm.h>
#include <fbl/span.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/ftl/raw_nand_image_utils.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image::ftl_image_internal {
namespace {

constexpr uint32_t kFtlUnsetPageMapping = std::numeric_limits<uint32_t>::max();

constexpr uint8_t GetByte(uint32_t index, uint32_t value) { return (value >> (index * 8) & 0xFF); }

TEST(FtlImageInternalTest, WriteOutOfBandBytesForVolumePageMatchesFormat) {
  uint32_t kLogicalPageNumber = 0xAABBCCDD;
  std::vector<uint8_t> oob_bytes(16, 0);

  std::vector<uint8_t> expected_oob_bytes(16, 0xFF);

  expected_oob_bytes[0] = 0xFF;

  // Virtual Page Number
  expected_oob_bytes[1] = GetByte(0, kLogicalPageNumber);
  expected_oob_bytes[2] = GetByte(1, kLogicalPageNumber);
  expected_oob_bytes[3] = GetByte(2, kLogicalPageNumber);
  expected_oob_bytes[4] = GetByte(3, kLogicalPageNumber);

  // Block Count / Generation Number to 0xFF
  expected_oob_bytes[5] = 0xFF;
  expected_oob_bytes[6] = 0xFF;
  expected_oob_bytes[7] = 0xFF;
  expected_oob_bytes[8] = 0xFF;

  // Wear count to zero.
  expected_oob_bytes[9] = 0;
  expected_oob_bytes[10] = 0;
  expected_oob_bytes[11] = 0;

  // Most significant nibble is used for wear count.
  expected_oob_bytes[12] = 0x0F;
  expected_oob_bytes[13] = 0xFF;
  expected_oob_bytes[14] = 0xFF;
  expected_oob_bytes[15] = kNdmVolumePageMark;

  WriteOutOfBandBytes<PageType::kVolumePage>(kLogicalPageNumber, oob_bytes);

  EXPECT_THAT(oob_bytes, testing::ElementsAreArray(expected_oob_bytes));
}

TEST(FtlImageInternalTest, WriteOutOfBandBytesForMapPageMatchesFormat) {
  uint32_t kLogicalPageNumber = 0xAABBDDEE;
  std::vector<uint8_t> oob_bytes(16, 0);

  std::vector<uint8_t> expected_oob_bytes(16, 0xFF);

  expected_oob_bytes[0] = 0xFF;

  // Virtual Page Number
  expected_oob_bytes[1] = GetByte(0, kLogicalPageNumber);
  expected_oob_bytes[2] = GetByte(1, kLogicalPageNumber);
  expected_oob_bytes[3] = GetByte(2, kLogicalPageNumber);
  expected_oob_bytes[4] = GetByte(3, kLogicalPageNumber);

  // Block Count / Generation Number to 0.
  expected_oob_bytes[5] = 0;
  expected_oob_bytes[6] = 0;
  expected_oob_bytes[7] = 0;
  expected_oob_bytes[8] = 0;

  // Wear count to zero.
  expected_oob_bytes[9] = 0;
  expected_oob_bytes[10] = 0;
  expected_oob_bytes[11] = 0;

  // Most significant nibble is used for wear count.
  expected_oob_bytes[12] = 0x0F;
  expected_oob_bytes[13] = 0xFF;
  expected_oob_bytes[14] = 0xFF;
  expected_oob_bytes[15] = kNdmVolumePageMark;

  WriteOutOfBandBytes<PageType::kMapPage>(kLogicalPageNumber, oob_bytes);

  EXPECT_THAT(oob_bytes, testing::ElementsAreArray(expected_oob_bytes));
}

class PageWriter final : public Writer {
 public:
  explicit PageWriter(uint64_t block_start, const RawNandOptions& options)
      : block_start_(block_start) {}

  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> buffer) final {
    if (offset < block_start_) {
      return fit::error("PageWriter write failed: Bad offset.");
    }

    uint8_t delta = offset - block_start_ - pages_.size();
    if (delta < 0) {
      std::fill_n(std::back_inserter(pages_), delta, 0xFF);
    }

    pages_.insert(pages_.end(), buffer.begin(), buffer.end());
    return fit::ok();
  }

  fbl::Span<const uint8_t> pages() const { return pages_; }

 private:
  uint64_t block_start_ = 0;
  std::vector<uint8_t> pages_;
};

std::vector<uint32_t> GetMappingsFromPage(fbl::Span<const uint8_t> contents,
                                          const RawNandOptions& options) {
  uint32_t mappings_per_page = options.page_size / sizeof(uint32_t);
  std::vector<uint32_t> actual_mappings;
  for (uint32_t i = 0; i < mappings_per_page; ++i) {
    size_t offset = i * 4;
    uint32_t value = contents[offset] | contents[offset + 1] << 8 | contents[offset + 2] << 16 |
                     contents[offset + 3] << 24;
    actual_mappings.push_back(value);
  }
  return actual_mappings;
}

TEST(FtlImageInternalTest, WriteMapBlockWritesAtOffsetWithSingleMapPageIsOk) {
  RawNandOptions options;
  options.pages_per_block = 2;
  options.oob_bytes_size = 16;
  // 5 mappings per page.
  options.page_size = 160;
  options.page_count = 4;

  // All pages are shifted by one.
  std::map<uint32_t, uint32_t> logical_to_physical_pages = {
      {0, 1},
      {1, 2},
      {2, 3},
      {3, 0},
  };

  // The page contents should be the following.
  std::vector<uint32_t> expected_page_contents = {1, 2, 3, 0};
  std::fill_n(std::back_inserter(expected_page_contents), 40 - expected_page_contents.size(),
              kFtlUnsetPageMapping);

  constexpr uint64_t kOffset = 12345;
  PageWriter writer(kOffset, options);

  auto write_result = WriteMapBlock(logical_to_physical_pages, options, kOffset, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  // 1 Map page.
  ASSERT_THAT(writer.pages(), testing::SizeIs(RawNandImageGetAdjustedPageSize(options)));

  std::vector<uint8_t> expected_oob_bytes(16, 0xFF);
  WriteOutOfBandBytes<PageType::kMapPage>(0, expected_oob_bytes);

  auto page_content = writer.pages().subspan(0, options.page_size);
  EXPECT_THAT(GetMappingsFromPage(page_content, options),
              testing::ElementsAreArray(expected_page_contents));

  auto actual_oob_bytes = writer.pages().subspan(options.page_size, options.oob_bytes_size);
  EXPECT_THAT(actual_oob_bytes, testing::ElementsAreArray(expected_oob_bytes));
}

TEST(FtlImageInternalTest, WriteMapBlockWritesAtOffsetWithMultipleMapPageIsOk) {
  RawNandOptions options;
  options.pages_per_block = 2;
  options.oob_bytes_size = 16;
  // 3 mappings per page.
  options.page_size = 12;
  options.page_count = 24;

  // All pages are shifted by one.
  std::map<uint32_t, uint32_t> logical_to_physical_pages = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, {23, 20},
  };

  // Only pages with mappings are written.
  struct MapPage {
    uint32_t logical_number = 0;
    std::vector<uint32_t> entries;
  };

  // Two pages, 3 mappings per page. Two extra pages set max value(-1).
  std::vector<MapPage> expected_pages = {
      {.logical_number = 0, .entries = {1, 2, 3}},
      {.logical_number = 1, .entries = {0, kFtlUnsetPageMapping, kFtlUnsetPageMapping}},
      {.logical_number = 7, .entries = {kFtlUnsetPageMapping, kFtlUnsetPageMapping, 20}},
  };

  constexpr uint64_t kOffset = 12345;
  PageWriter writer(kOffset, options);

  auto write_result = WriteMapBlock(logical_to_physical_pages, options, kOffset, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  ASSERT_THAT(writer.pages(),
              testing::SizeIs(expected_pages.size() * RawNandImageGetAdjustedPageSize(options)));

  for (size_t map_page = 0; map_page < expected_pages.size(); ++map_page) {
    const auto& expected_map_page = expected_pages[map_page];
    std::vector<uint8_t> expected_oob_bytes(16, 0xFF);
    WriteOutOfBandBytes<PageType::kMapPage>(expected_map_page.logical_number, expected_oob_bytes);
    auto adjusted_page = writer.pages().subspan(RawNandImageGetPageOffset(map_page, options));
    // auto oob_bytes = adjusted_page.subspan(page_contents.size(), options.oob_bytes_size);
    auto page_content = adjusted_page.subspan(0, options.page_size);
    EXPECT_THAT(GetMappingsFromPage(page_content, options),
                testing::ElementsAreArray(expected_map_page.entries));

    auto actual_oob_bytes = adjusted_page.subspan(options.page_size, options.oob_bytes_size);
    EXPECT_THAT(actual_oob_bytes, testing::ElementsAreArray(expected_oob_bytes)) << map_page;
  }
}

TEST(FtlImageInternalTest, WriteMapBlockOnWriterErrorForwardsWriterErrors) {
  RawNandOptions options;
  options.pages_per_block = 2;
  options.oob_bytes_size = 16;
  // 3 mappings per page.
  options.page_size = 12;
  options.page_count = 24;

  // All pages are shifted by one.
  std::map<uint32_t, uint32_t> logical_to_physical_pages = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, {23, 20},
  };

  constexpr uint64_t kOffset = 12345;
  PageWriter writer(kOffset, options);

  auto write_result = WriteMapBlock(logical_to_physical_pages, options, kOffset - 1, &writer);
  ASSERT_TRUE(write_result.is_error());
}

// A Map page uses 32 bit integers to map a page, the page size must be greater than this so it is
// feasible.
TEST(FtlImageInternalTest, WriteMapBlockWithPageSizeSmallerThanPageMappingSizeIsError) {
  RawNandOptions options;
  options.pages_per_block = 2;
  options.oob_bytes_size = 16;
  // 3 mappings per page.
  options.page_size = 3;
  options.page_count = 24;

  // All pages are shifted by one.
  std::map<uint32_t, uint32_t> logical_to_physical_pages = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, {23, 20},
  };

  constexpr uint64_t kOffset = 12345;
  PageWriter writer(kOffset, options);

  auto write_result = WriteMapBlock(logical_to_physical_pages, options, kOffset, &writer);
  ASSERT_TRUE(write_result.is_error());
}

TEST(FtlImageInternalTest, WriteMapBlockWithOOBBytesSmallerThanPageMappingSizeIsError) {
  RawNandOptions options;
  options.pages_per_block = 2;
  options.oob_bytes_size = 15;
  // 3 mappings per page.
  options.page_size = 4;
  options.page_count = 24;

  // All pages are shifted by one.
  std::map<uint32_t, uint32_t> logical_to_physical_pages = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, {23, 20},
  };

  constexpr uint64_t kOffset = 12345;
  PageWriter writer(kOffset, options);

  auto write_result = WriteMapBlock(logical_to_physical_pages, options, kOffset, &writer);
  ASSERT_TRUE(write_result.is_error());
}

}  // namespace
}  // namespace storage::volume_image::ftl_image_internal
