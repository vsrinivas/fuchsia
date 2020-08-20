// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/ftl_raw_nand_image_writer.h"

#include <lib/fit/result.h>

#include <array>
#include <cinttypes>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <fbl/span.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/ftl/raw_nand_image.h"
#include "src/storage/volume_image/ftl/raw_nand_image_utils.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace {

constexpr uint32_t kPageSize = 8;
constexpr uint32_t kOobBytesSize = 9;
constexpr uint32_t kPagesPerBlock = 16;
constexpr uint32_t kBlockCount = 5;
constexpr uint32_t kPageCount = kPagesPerBlock * kBlockCount;
constexpr ImageFormat kFormat = ImageFormat::kRawImage;
constexpr std::array<RawNandImageFlag, 1> kFlags = {RawNandImageFlag::kRequireWipeBeforeFlash};

struct RawNandPage {
  std::vector<uint8_t> data_;
  std::vector<uint8_t> oob_;
};

class RamRawNandImageWriter : public Writer {
 public:
  explicit RamRawNandImageWriter(RawNandOptions options) : options_(options) {}

  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> data) final {
    uint32_t data_offset = 0;
    if (offset < sizeof(RawNandImageHeader)) {
      data_offset = sizeof(RawNandImageHeader) - offset;
      if (data.size() < data_offset) {
        data_offset = data.size();
      }
      auto header_data = data.subspan(offset, data_offset);
      memcpy(reinterpret_cast<uint8_t*>(&header_) + offset, header_data.data(), header_data.size());
    }

    // No image data write.
    if (data_offset >= data.size()) {
      return fit::ok();
    }

    uint64_t image_offset = offset - sizeof(RawNandImageHeader);
    uint64_t image_page_offset = image_offset % RawNandImageGetAdjustedPageSize(options_);
    uint64_t image_page_number = image_offset / RawNandImageGetAdjustedPageSize(options_);

    // Its a page write.
    if (image_page_offset == 0) {
      if (data.size() != options_.page_size) {
        return fit::error("Bad page data buffer.");
      }
      pages_[image_page_number].data_ = std::vector(data.begin(), data.end());
      return fit::ok();
    }

    // Its oob data.
    if (image_page_offset == options_.page_size) {
      if (data.size() != options_.oob_bytes_size) {
        return fit::error("Bad oob buffer size.");
      }

      pages_[image_page_number].oob_ = std::vector(data.begin(), data.end());
      return fit::ok();
    }

    return fit::error("Unaligned page write.");
  }

  const auto& pages() { return pages_; }

  const auto& header() const { return header_; }

 private:
  std::map<uint32_t, RawNandPage> pages_;

  RawNandImageHeader header_;

  RawNandOptions options_;
};

RawNandOptions MakeOptions() {
  RawNandOptions options;
  options.oob_bytes_size = kOobBytesSize;
  options.page_size = kPageSize;
  options.page_count = kPageCount;
  options.pages_per_block = kPagesPerBlock;

  return options;
}

TEST(FtlRawNandImageWriterTest, CreateWithoutWriterIsError) {
  ASSERT_TRUE(FtlRawNandImageWriter::Create(MakeOptions(), kFlags, kFormat, nullptr).is_error());
}

TEST(FtlRawNandImageWriterTest, CreateWithZeroOobSizeIsError) {
  RawNandOptions device_options = MakeOptions();
  RamRawNandImageWriter writer(device_options);
  device_options.oob_bytes_size = 0;

  ASSERT_TRUE(FtlRawNandImageWriter::Create(device_options, kFlags, kFormat, &writer).is_error());
}

TEST(FtlRawNandImageWriterTest, CreateWithZeroPagesPerBlockIsError) {
  RawNandOptions device_options = MakeOptions();
  device_options.pages_per_block = 0;
  RamRawNandImageWriter writer(device_options);

  ASSERT_TRUE(FtlRawNandImageWriter::Create(device_options, kFlags, kFormat, &writer).is_error());
}

TEST(FtlRawNandImageWriterTest, CreateWithNotEnoughOObPerBlockIsError) {
  RawNandOptions device_options = MakeOptions();
  device_options.pages_per_block = 2;
  device_options.oob_bytes_size = 1;
  RamRawNandImageWriter writer(device_options);

  ASSERT_TRUE(FtlRawNandImageWriter::Create(device_options, kFlags, kFormat, &writer).is_error());
}

TEST(FtlRawNandImageWriterTest, CreateWithValidOptionsAndWriterIsOkAndProducesCorrectFtlOptions) {
  RawNandOptions device_options = MakeOptions();
  RamRawNandImageWriter writer(device_options);

  auto writer_or = FtlRawNandImageWriter::Create(device_options, kFlags, kFormat, &writer);
  ASSERT_TRUE(writer_or.is_ok()) << writer_or.error();

  auto [raw_image_writer, ftl_options] = writer_or.take_value();

  ASSERT_EQ(raw_image_writer.scale_factor(), 2);

  EXPECT_EQ(ftl_options.oob_bytes_size,
            device_options.oob_bytes_size * raw_image_writer.scale_factor());
  EXPECT_EQ(ftl_options.page_size, device_options.page_size * raw_image_writer.scale_factor());
  EXPECT_EQ(ftl_options.page_count, device_options.page_count / raw_image_writer.scale_factor());
  EXPECT_EQ(ftl_options.pages_per_block,
            device_options.pages_per_block / raw_image_writer.scale_factor());

  const auto& header = writer.header();

  EXPECT_EQ(header.magic, RawNandImageHeader::kMagic);
  EXPECT_EQ(header.version_major, RawNandImageHeader::kMajorVersion);
  EXPECT_EQ(header.version_minor, RawNandImageHeader::kMinorVersion);
  EXPECT_NE(header.flags & static_cast<std::underlying_type<RawNandImageFlag>::type>(kFlags[0]),
            static_cast<std::underlying_type<RawNandImageFlag>::type>(0));
  EXPECT_EQ(header.format, kFormat);
  EXPECT_EQ(header.page_size, device_options.page_size);
  EXPECT_EQ(header.oob_size, device_options.oob_bytes_size);
  EXPECT_THAT(header.reserved, testing::Each(testing::Eq(0xFF)));
}

TEST(FtlRawNandImageWriterTest, WriteWithUnAlignedOffsetIsError) {
  RawNandOptions device_options = MakeOptions();
  RamRawNandImageWriter writer(device_options);

  auto writer_or = FtlRawNandImageWriter::Create(device_options, kFlags, kFormat, &writer);
  ASSERT_TRUE(writer_or.is_ok()) << writer_or.error();

  auto [raw_image_writer, ftl_options] = writer_or.take_value();
  std::vector<uint8_t> page_buffer(ftl_options.page_size, 0xFF);
  std::vector<uint8_t> oob_buffer(ftl_options.oob_bytes_size, 0xFF);

  EXPECT_TRUE(raw_image_writer.Write(1, page_buffer).is_error());
  EXPECT_TRUE(raw_image_writer.Write(ftl_options.page_size + 1, oob_buffer).is_error());
}

TEST(FtlRawNandImageWriterTest, WriteAtAlignedOffsetWithWrongBufferSizeIsError) {
  RawNandOptions device_options = MakeOptions();
  RamRawNandImageWriter writer(device_options);

  auto writer_or = FtlRawNandImageWriter::Create(device_options, kFlags, kFormat, &writer);
  ASSERT_TRUE(writer_or.is_ok()) << writer_or.error();

  auto [raw_image_writer, ftl_options] = writer_or.take_value();
  std::vector<uint8_t> page_buffer(ftl_options.page_size - 1, 0xFF);
  std::vector<uint8_t> oob_buffer(ftl_options.oob_bytes_size + 1, 0xFF);

  EXPECT_TRUE(raw_image_writer.Write(0, page_buffer).is_error());
  EXPECT_TRUE(raw_image_writer.Write(ftl_options.page_size, oob_buffer).is_error());
}

// Fills |buffer| with a sequence starting at shift up to upper limit, and then jumps back to zero.
void FillBuffer(fbl::Span<uint8_t> buffer, uint64_t shift) {
  for (auto& b : buffer) {
    b = shift;
    shift = (shift + 1) % std::numeric_limits<uint8_t>::max();
  }
}

TEST(FtlRawNandImageWriterTest, WriteAtAlignedOffsetWithExpectedBufferSizeIsOk) {
  constexpr int kLogicalPagesToWrite = 10;
  RawNandOptions device_options = MakeOptions();
  RamRawNandImageWriter writer(device_options);

  auto writer_or = FtlRawNandImageWriter::Create(device_options, kFlags, kFormat, &writer);
  ASSERT_TRUE(writer_or.is_ok()) << writer_or.error();

  auto [raw_image_writer, ftl_options] = writer_or.take_value();
  std::vector<uint8_t> page_buffer(ftl_options.page_size, 0xFF);
  std::vector<uint8_t> oob_buffer(ftl_options.oob_bytes_size, 0xFF);

  for (uint32_t i = 0; i < kLogicalPagesToWrite; ++i) {
    FillBuffer(page_buffer, i);
    FillBuffer(oob_buffer, i + device_options.oob_bytes_size);

    auto page_write_result =
        raw_image_writer.Write(RawNandImageGetPageOffset(i, ftl_options), page_buffer);
    ASSERT_TRUE(page_write_result.is_ok()) << page_write_result.error();

    auto oob_write_result = raw_image_writer.Write(
        RawNandImageGetPageOffset(i, ftl_options) + ftl_options.page_size, oob_buffer);
    ASSERT_TRUE(oob_write_result.is_ok()) << oob_write_result.error();
  }

  // Check header
  const auto& header = writer.header();

  EXPECT_EQ(header.magic, RawNandImageHeader::kMagic);
  EXPECT_EQ(header.version_major, RawNandImageHeader::kMajorVersion);
  EXPECT_EQ(header.version_minor, RawNandImageHeader::kMinorVersion);
  EXPECT_NE(header.flags & static_cast<std::underlying_type<RawNandImageFlag>::type>(kFlags[0]),
            static_cast<std::underlying_type<RawNandImageFlag>::type>(0));
  EXPECT_EQ(header.format, kFormat);
  EXPECT_EQ(header.page_size, device_options.page_size);
  EXPECT_EQ(header.oob_size, device_options.oob_bytes_size);
  EXPECT_THAT(header.reserved, testing::Each(testing::Eq(0xFF)));

  for (uint32_t i = 0; i < kLogicalPagesToWrite; ++i) {
    SCOPED_TRACE("Logical Page " + std::to_string(i));
    FillBuffer(page_buffer, i);
    FillBuffer(oob_buffer, i + device_options.oob_bytes_size);
    auto page_view = fbl::Span<const uint8_t>(page_buffer);
    auto oob_view = fbl::Span<const uint8_t>(oob_buffer);

    EXPECT_THAT(writer.pages().at(2 * i).data_,
                testing::ElementsAreArray(page_view.subspan(0, device_options.page_size)));
    EXPECT_THAT(writer.pages().at(2 * i + 1).data_,
                testing::ElementsAreArray(
                    page_view.subspan(device_options.page_size, device_options.page_size)));

    EXPECT_THAT(writer.pages().at(2 * i).oob_,
                testing::ElementsAreArray(oob_view.subspan(0, device_options.oob_bytes_size)));
    EXPECT_THAT(writer.pages().at(2 * i + 1).oob_,
                testing::ElementsAreArray(oob_view.subspan(device_options.oob_bytes_size,
                                                           device_options.oob_bytes_size)));
  }
}

}  // namespace
}  // namespace storage::volume_image
