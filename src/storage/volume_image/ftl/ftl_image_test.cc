// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/ftl_image.h"

#include <lib/fit/function.h>

#include <cstdint>
#include <iterator>
#include <string>
#include <variant>

#include <fbl/algorithm.h>
#include <fbl/span.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/ftl/ftl_image_internal.h"
#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/ftl/raw_nand_image_utils.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/partition.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {

constexpr uint32_t kFtlUnsetPageMapping = std::numeric_limits<uint32_t>::max();

class FakeWriter final : public Writer {
 public:
  // On success data backing this writer is updated at [|offset|, |offset| +
  // |buffer.size()|] to |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> buffer) final {
    if (offset > pages_.size()) {
      std::fill_n(std::back_inserter(pages_), offset - pages_.size(), -1);
    }
    pages_.insert(pages_.end(), buffer.begin(), buffer.end());
    return fit::ok();
  }

  fbl::Span<const uint8_t> pages() const { return pages_; }

 private:
  std::vector<uint8_t> pages_;

  // Options for the nand device.
  RawNandOptions options_;
};

void VisitBlocksOnBuffer(
    uint32_t block_size, uint64_t offset, fbl::Span<uint8_t> buffer,
    fit::function<void(uint32_t block_number, fbl::Span<uint8_t> block_view)> visitor) {
  uint64_t block_start = GetBlockFromBytes(offset, block_size);
  uint64_t block_count = GetBlockCount(offset, buffer.size(), block_size);

  if (block_count == 0) {
    return;
  }

  size_t offset_from_start = GetOffsetFromBlockStart(offset, block_size);

  // Fill first block, might not be aligned.
  auto first_block_view =
      buffer.subspan(0, std::min(buffer.size(), block_size - offset_from_start));
  visitor(block_start, first_block_view);

  // Fill all remaning blocks are aligned from this point of view.
  for (size_t i = 0; i < block_count - 1; ++i) {
    uint64_t buffer_offset = first_block_view.size() + i * block_size;
    uint32_t remaining_bytes = buffer.size() - buffer_offset - i * block_size;

    auto aligned_block_view = buffer.subspan(buffer_offset, std::min(remaining_bytes, block_size));
    visitor(block_start + i, aligned_block_view);
  }
}

// Will fill each block with a pattern based on the requested block number.
class FakeReader final : public Reader {
 public:
  static void FillBlock(uint64_t block_number, fbl::Span<uint8_t> buffer) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&block_number);

    for (size_t i = 0; i < buffer.size(); ++i) {
      buffer[i] = bytes[i % sizeof(uint64_t)];
    }
  }

  explicit FakeReader(uint32_t block_size) : block_size_(block_size) {}

  uint64_t GetMaximumOffset() const override { return 0; }

  // On success data backing this writer is updated at [|offset|, |offset| +
  // |buffer.size()|] to |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  fit::result<void, std::string> Read(uint64_t offset, fbl::Span<uint8_t> buffer) const final {
    VisitBlocksOnBuffer(block_size_, offset, buffer, FillBlock);
    return fit::ok();
  }

 private:
  uint32_t block_size_ = 0;
};

[[maybe_unused]] std::vector<uint8_t> GetBlockContents(uint64_t offset, uint32_t size) {
  std::vector<uint8_t> contents(size, 0xFF);
  FakeReader::FillBlock(offset, contents);
  return contents;
}

VolumeDescriptor MakeVolumeDescriptor() {
  VolumeDescriptor descriptor;
  descriptor.block_size = 32;

  return descriptor;
}

// Only pages with mappings are written.
struct MapPage {
  uint32_t logical_number = 0;
  std::vector<uint32_t> entries;
};

void CheckMapPage(const MapPage& expected_map_page, fbl::Span<const uint8_t> actual_contents,
                  const RawNandOptions& options) {
  auto expected_map_page_contents =
      fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(expected_map_page.entries.data()),
                               expected_map_page.entries.size() * sizeof(uint32_t));

  std::vector<uint8_t> expected_oob(16, 0xFF);
  ftl_image_internal::WriteOutOfBandBytes<ftl_image_internal::PageType::kMapPage>(
      expected_map_page.logical_number, expected_oob);

  auto actual_page = actual_contents.subspan(0, options.page_size);

  auto actual_oob = actual_contents.subspan(options.page_size, options.oob_bytes_size);
  EXPECT_THAT(actual_oob, testing::ElementsAreArray(expected_oob));

  // Treat as fatal failure so callers can abort.
  ASSERT_THAT(actual_page, testing::ElementsAreArray(expected_map_page_contents));
}

void CheckVolumePage(uint64_t source_offset, uint64_t target_offset, uint64_t length,
                     uint32_t logical_page_number, uint32_t physical_page_number,
                     const RawNandOptions& options, const Reader* reader,
                     fbl::Span<const uint8_t> contents) {
  std::vector<uint8_t> expected_page(options.page_size, 0xFF);
  std::vector<uint8_t> expected_oob(options.oob_bytes_size, 0xFF);

  uint64_t offset_from_page = GetOffsetFromBlockStart(target_offset, options.page_size);
  uint64_t page_offset = RawNandImageGetPageOffset(physical_page_number, options);

  auto page_view = contents.subspan(page_offset + offset_from_page, length);
  auto oob_view = contents.subspan(page_offset + options.page_size, options.oob_bytes_size);

  ASSERT_TRUE(reader->Read(source_offset, expected_page).is_ok());
  ftl_image_internal::WriteOutOfBandBytes<ftl_image_internal::PageType::kVolumePage>(
      logical_page_number, expected_oob);

  EXPECT_THAT(oob_view, testing::ElementsAreArray(expected_oob));
  ASSERT_THAT(page_view,
              testing::ElementsAreArray(fbl::Span<uint8_t>(expected_page).subspan(0, length)));
}

void CheckVolumePagesInMapping(const AddressMap& mapping, const RawNandOptions& options,
                               uint32_t logical_page_start, uint32_t physical_page_start,
                               const Reader* reader, fbl::Span<const uint8_t> contents) {
  uint64_t read_bytes = 0;
  uint64_t page_count = GetBlockCount(mapping.target, mapping.count, options.page_size);

  for (uint32_t page_offset = 0; page_offset < page_count; ++page_offset) {
    uint64_t target_offset = mapping.target + read_bytes;
    uint64_t source_offset = mapping.source + read_bytes;
    uint64_t length =
        std::min(options.page_size - GetOffsetFromBlockStart(target_offset, options.page_size),
                 mapping.count - read_bytes);

    CheckVolumePage(source_offset, target_offset, length, logical_page_start + page_offset,
                    physical_page_start + page_offset, options, reader, contents);
    read_bytes += length;
  }
}

TEST(FtlImageTest, FtlImageWriteWithASinglePageAlignedMappingIsOk) {
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {{.source = 32, .target = 128, .count = 16}};

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  MapPage map_page = {
      .logical_number = 2,
      .entries = {0, kFtlUnsetPageMapping, kFtlUnsetPageMapping, kFtlUnsetPageMapping}};

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 1;
  uint64_t expected_map_pages = 1;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) *
      RawNandImageGetAdjustedPageSize(options);
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * RawNandImageGetAdjustedPageSize(options);

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  // Check the map page.
  CheckMapPage(map_page,
               view.subspan(expected_map_page_offset, RawNandImageGetAdjustedPageSize(options)),
               options);
}

TEST(FtlImageTest, FtlImageWriteWithMultipleMappingsSharingPagesIsError) {
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {
      {.source = 32, .target = 0, .count = 16},
      {.source = 32, .target = 128, .count = 12},
      // This mapping shares pages with the previous one.
      {.source = 32, .target = 140, .count = 48},
  };

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_error());
}

TEST(FtlImageTest, FtlImageWriteWithMultiplePageAlignedMappingIsOk) {
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {{.source = 32, .target = 128, .count = 48}};

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  MapPage map_page = {.logical_number = 2, .entries = {0, 1, 2, kFtlUnsetPageMapping}};

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 3;
  uint64_t expected_map_pages = 1;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) *
      RawNandImageGetAdjustedPageSize(options);
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * RawNandImageGetAdjustedPageSize(options);

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  // Check the map page.
  CheckMapPage(map_page,
               view.subspan(expected_map_page_offset, RawNandImageGetAdjustedPageSize(options)),
               options);
}

TEST(FtlImageTest, FtlImageWriteWithMultipleAlignedMappingsIsOk) {
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {
      {.source = 32, .target = 128, .count = 48},
      {.source = 16, .target = 96, .count = 32},
      {.source = 80, .target = 80, .count = 16},
  };

  uint32_t adjusted_page_size = RawNandImageGetAdjustedPageSize(options);

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  std::vector<MapPage> map_pages = {
      {.logical_number = 1, .entries = {kFtlUnsetPageMapping, 5, 3, 4}},
      {.logical_number = 2, .entries = {0, 1, 2, kFtlUnsetPageMapping}},
  };

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 6;
  uint64_t expected_map_pages = 2;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) * adjusted_page_size;
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * adjusted_page_size;

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());
  CheckVolumePagesInMapping(partition.address().mappings[1], options, 6, 3, partition.reader(),
                            writer.pages());
  CheckVolumePagesInMapping(partition.address().mappings[2], options, 5, 5, partition.reader(),
                            writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  for (uint32_t i = 0; i < map_pages.size(); ++i) {
    const auto& map_page = map_pages[i];
    // Check the map page.
    CheckMapPage(
        map_page,
        view.subspan(expected_map_page_offset + i * adjusted_page_size, adjusted_page_size),
        options);
  }
}

TEST(FtlImageTest, FtlImageWriteWithASinglePageUnalignedMappingIsOk) {
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {{.source = 32, .target = 129, .count = 15}};

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  MapPage map_page = {
      .logical_number = 2,
      .entries = {0, kFtlUnsetPageMapping, kFtlUnsetPageMapping, kFtlUnsetPageMapping}};

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 1;
  uint64_t expected_map_pages = 1;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) *
      RawNandImageGetAdjustedPageSize(options);
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * RawNandImageGetAdjustedPageSize(options);

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  // Check the map page.
  CheckMapPage(map_page,
               view.subspan(expected_map_page_offset, RawNandImageGetAdjustedPageSize(options)),
               options);
}

TEST(FtlImageTest, FtlImageWriteWithAMultiplePageUnalignedMappingIsOk) {
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {{.source = 32, .target = 129, .count = 17}};

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  MapPage map_page = {.logical_number = 2,
                      .entries = {0, 1, kFtlUnsetPageMapping, kFtlUnsetPageMapping}};

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 2;
  uint64_t expected_map_pages = 1;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) *
      RawNandImageGetAdjustedPageSize(options);
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * RawNandImageGetAdjustedPageSize(options);

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  // Check the map page.
  CheckMapPage(map_page,
               view.subspan(expected_map_page_offset, RawNandImageGetAdjustedPageSize(options)),
               options);
}

TEST(FtlImageTest, FtlImageWriteWithAMultiplePageUnalignedAndMultipleMappingsIsOk) {
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {
      {.source = 32, .target = 129, .count = 43},
      {.source = 16, .target = 97, .count = 26},
      {.source = 80, .target = 81, .count = 9},
  };

  uint32_t adjusted_page_size = RawNandImageGetAdjustedPageSize(options);

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  std::vector<MapPage> map_pages = {
      {.logical_number = 1, .entries = {kFtlUnsetPageMapping, 5, 3, 4}},
      {.logical_number = 2, .entries = {0, 1, 2, kFtlUnsetPageMapping}},
  };

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 6;
  uint64_t expected_map_pages = 2;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) * adjusted_page_size;
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * adjusted_page_size;

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());
  CheckVolumePagesInMapping(partition.address().mappings[1], options, 6, 3, partition.reader(),
                            writer.pages());
  CheckVolumePagesInMapping(partition.address().mappings[2], options, 5, 5, partition.reader(),
                            writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  for (uint32_t i = 0; i < map_pages.size(); ++i) {
    const auto& map_page = map_pages[i];
    // Check the map page.
    CheckMapPage(
        map_page,
        view.subspan(expected_map_page_offset + i * adjusted_page_size, adjusted_page_size),
        options);
  }
}

TEST(FtlImageTest, FtlImageWriteWithAMultiplePagesAndMultipleMappingsIsOk) {
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {
      {.source = 32, .target = 128, .count = 43},
      {.source = 16, .target = 97, .count = 31},
      {.source = 80, .target = 81, .count = 15},
  };

  uint32_t adjusted_page_size = RawNandImageGetAdjustedPageSize(options);

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  std::vector<MapPage> map_pages = {
      {.logical_number = 1, .entries = {kFtlUnsetPageMapping, 5, 3, 4}},
      {.logical_number = 2, .entries = {0, 1, 2, kFtlUnsetPageMapping}},
  };

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 6;
  uint64_t expected_map_pages = 2;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) * adjusted_page_size;
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * adjusted_page_size;

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());
  CheckVolumePagesInMapping(partition.address().mappings[1], options, 6, 3, partition.reader(),
                            writer.pages());
  CheckVolumePagesInMapping(partition.address().mappings[2], options, 5, 5, partition.reader(),
                            writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  for (uint32_t i = 0; i < map_pages.size(); ++i) {
    const auto& map_page = map_pages[i];
    // Check the map page.
    CheckMapPage(
        map_page,
        view.subspan(expected_map_page_offset + i * adjusted_page_size, adjusted_page_size),
        options);
  }
}

TEST(FtlImageTest, FtlImageWriteWithBiggerSizeThanMappingAndNoFillingHasNoEffectIsOk) {
  // The FTL doesn't need to map pages that need to be 'allocated' but not written, since this will
  // done lazily when trying to write into the desired location.
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {{.source = 32, .target = 128, .count = 16, .size = 32}};

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  MapPage map_page = {
      .logical_number = 2,
      .entries = {0, kFtlUnsetPageMapping, kFtlUnsetPageMapping, kFtlUnsetPageMapping}};

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 1;
  uint64_t expected_map_pages = 1;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) *
      RawNandImageGetAdjustedPageSize(options);
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * RawNandImageGetAdjustedPageSize(options);

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  // Check the map page.
  CheckMapPage(map_page,
               view.subspan(expected_map_page_offset, RawNandImageGetAdjustedPageSize(options)),
               options);
}

class ZeroReader final : public Reader {
 public:
  uint64_t GetMaximumOffset() const override { return 0; }

  fit::result<void, std::string> Read(uint64_t offset, fbl::Span<uint8_t> buffer) const final {
    std::fill(buffer.begin(), buffer.end(), 0);
    return fit::ok();
  }
};

TEST(FtlImageTest, FtlImageWriteWithBiggerSizeThanMappingAndWithFillingMapsZeroedPagesAndIsOk) {
  // The FTL doesn't need to map pages that need to be 'allocated' but not written, since this will
  // done lazily when trying to write into the desired location.
  RawNandOptions options;
  options.oob_bytes_size = 16;
  options.page_size = 16;
  options.page_count = 100;
  options.pages_per_block = 4;

  auto volume_descriptor = MakeVolumeDescriptor();
  AddressDescriptor address_descriptor;
  address_descriptor.mappings = {{.source = 32,
                                  .target = 128,
                                  .count = 16,
                                  .size = 50,
                                  .options = {{EnumAsString(AddressMapOption::kFill), 0}}}};

  // Two pages, 4 mappings per page. Two extra pages set max value(-1).
  MapPage map_page = {.logical_number = 2, .entries = {0, 1, 2, 3}};

  auto reader = std::make_unique<FakeReader>(volume_descriptor.block_size);
  auto partition = Partition(volume_descriptor, address_descriptor, std::move(reader));

  // One volume page and one map page.
  uint64_t expected_volume_page_count = 4;
  uint64_t expected_map_pages = 1;

  // Map pages should be on a different block than volume pages.
  uint64_t expected_map_page_offset =
      fbl::round_up(expected_volume_page_count, options.pages_per_block) *
      RawNandImageGetAdjustedPageSize(options);
  uint64_t written_content_size =
      expected_map_page_offset + expected_map_pages * RawNandImageGetAdjustedPageSize(options);

  FakeWriter writer;
  auto write_result = FtlImageWrite(options, partition, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_THAT(writer.pages(), testing::SizeIs(written_content_size));

  auto view = fbl::Span<const uint8_t>(writer.pages());

  // Check volume page, which should be the first physical page written plus OOB size.
  CheckVolumePagesInMapping(partition.address().mappings[0], options, 8, 0, partition.reader(),
                            writer.pages());

  // Check zeroed volume pages, which when the fill option is set,
  AddressMap zeroed_mapping = {.source = 48, .target = 144, .count = 34};
  ZeroReader zero_reader;
  CheckVolumePagesInMapping(zeroed_mapping, options, 9, 1, &zero_reader, writer.pages());

  // Check that everything in between is 0xFF, so there are no unexpected values.
  uint64_t skipped_offset = expected_volume_page_count * RawNandImageGetAdjustedPageSize(options);
  auto not_written_contents =
      view.subspan(skipped_offset, expected_map_page_offset - skipped_offset);
  EXPECT_THAT(not_written_contents, testing::Each(testing::Eq(0xFF)));

  // Check the map page.
  CheckMapPage(map_page,
               view.subspan(expected_map_page_offset, RawNandImageGetAdjustedPageSize(options)),
               options);
}

}  // namespace
}  // namespace storage::volume_image
