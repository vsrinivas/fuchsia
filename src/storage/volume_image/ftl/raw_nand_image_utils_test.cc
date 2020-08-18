// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/raw_nand_image_utils.h"

#include <cstdint>
#include <limits>

#include <fbl/span.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace {

class BufferWriter final : public Writer {
 public:
  explicit BufferWriter(size_t size) : buffer_(size, std::numeric_limits<uint8_t>::max()) {}

  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> buffer) final {
    if (offset + buffer.size() > buffer_.size()) {
      return fit::error("Out of Range");
    }
    memcpy(buffer_.data() + offset, buffer.data(), buffer.size());
    return fit::ok();
  }

  fbl::Span<const uint8_t> data() const { return buffer_; }

 private:
  std::vector<uint8_t> buffer_;
};

TEST(RawNandImageUtilsTest, RawNandImageGetPageOffsetAccountsForOOBByteSize) {
  RawNandOptions options;
  options.page_size = 4096;
  options.oob_bytes_size = 8;
  options.pages_per_block = 64;

  EXPECT_EQ(RawNandImageGetPageOffset(0, options), 0u);
  EXPECT_EQ(RawNandImageGetPageOffset(1, options), 4104u);
  EXPECT_EQ(RawNandImageGetPageOffset(2, options), 8208u);
}

TEST(RawNandImageUtilsTest, RawNandImageGetNextEraseBlockOffsetWhenStartIsTheOffset) {
  RawNandOptions options;
  options.page_size = 4096;
  options.oob_bytes_size = 8;
  options.pages_per_block = 64;

  EXPECT_EQ(RawNandImageGetNextEraseBlockOffset(0, options), 0u);
  EXPECT_EQ(RawNandImageGetNextEraseBlockOffset(4104u * 64, options), 4104u * 64);
  EXPECT_EQ(RawNandImageGetNextEraseBlockOffset(8208u * 64, options), 4104u * 2 * 64);
}

TEST(RawNandImageUtilsTest, RawNandImageGetNextEraseBlockOffsetBumpsToNextBlockStartWhenUnaligned) {
  RawNandOptions options;
  options.page_size = 4096;
  options.oob_bytes_size = 8;
  options.pages_per_block = 64;

  EXPECT_EQ(RawNandImageGetNextEraseBlockOffset(1, options), 4104u * 64);
  EXPECT_EQ(RawNandImageGetNextEraseBlockOffset(4104u * 64 + 1, options), 4104u * 2 * 64);
  EXPECT_EQ(RawNandImageGetNextEraseBlockOffset(4104u * 2 * 64 + 1, options), 4104u * 3 * 64);
}

TEST(RawNandImageUtilsTest, RawNandImageWritePageCompliesWithFormat) {
  constexpr uint64_t kWriterOffset = 32;
  std::vector<uint8_t> buffer(24, 0xFF);
  auto page = fbl::Span<uint8_t>(buffer).subspan(0, 16);
  auto oob = fbl::Span<uint8_t>(buffer).subspan(16, 8);

  BufferWriter writer(kWriterOffset + buffer.size());

  std::fill(page.begin(), page.end(), 0xAB);
  std::fill(oob.begin(), oob.end(), 0xCD);

  auto write_result = RawNandImageWritePage(page, oob, kWriterOffset, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  // This should check that data is followed by the OOB bytes.
  EXPECT_THAT(writer.data().subspan(kWriterOffset), testing::ElementsAreArray(buffer));
}

TEST(RawNandImageUtilsTest, RawNandImageWriteReturnsErrors) {
  constexpr uint64_t kWriterOffset = 32;
  std::vector<uint8_t> buffer(24, 0xFF);
  auto page = fbl::Span<uint8_t>(buffer).subspan(0, 16);
  auto oob = fbl::Span<uint8_t>(buffer).subspan(16, 8);

  BufferWriter writer(kWriterOffset);

  std::fill(page.begin(), page.end(), 0xAB);
  std::fill(oob.begin(), oob.end(), 0xCD);

  auto write_result = RawNandImageWritePage(page, oob, kWriterOffset, &writer);
  ASSERT_TRUE(write_result.is_error());
}

}  // namespace
}  // namespace storage::volume_image
