// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/block_writer.h"

#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>
#include <string.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace {

class FakeReader final : public Reader {
 public:
  explicit FakeReader(cpp20::span<const uint8_t> data, uint64_t block_size)
      : data_(data), block_size_(block_size) {}

  uint64_t length() const final { return data_.size(); }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (offset % block_size_ != 0) {
      return fpromise::error("Offset(" + std::to_string(offset) +
                             ") must be block aligned(block_size:" + std::to_string(block_size_) +
                             " ).");
    }

    if (buffer.size() % block_size_ != 0) {
      return fpromise::error("Buffer size(" + std::to_string(buffer.size()) +
                             ") must be block aligned(block_size:" + std::to_string(block_size_) +
                             " ).");
    }

    if (offset + buffer.size() > length()) {
      return fpromise::error("FakeReader::Read OOB read.");
    }
    memcpy(buffer.data(), data_.data() + offset, buffer.size());
    return fpromise::ok();
  }

 private:
  cpp20::span<const uint8_t> data_;
  uint64_t block_size_;
};

class FakeWriter final : public Writer {
 public:
  explicit FakeWriter(cpp20::span<uint8_t> data, uint64_t block_size)
      : data_(data), block_size_(block_size) {}

  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    if (offset % block_size_ != 0) {
      return fpromise::error("Offset(" + std::to_string(offset) +
                             ") must be block aligned(block_size:" + std::to_string(block_size_) +
                             " ).");
    }

    if (buffer.size() % block_size_ != 0) {
      return fpromise::error("Buffer size(" + std::to_string(buffer.size()) +
                             ") must be block aligned(block_size:" + std::to_string(block_size_) +
                             " ).");
    }

    if (offset + buffer.size() > data_.size()) {
      return fpromise::error("FakeWriter::Write OOB write.");
    }
    memcpy(data_.data() + offset, buffer.data(), buffer.size());
    return fpromise::ok();
  }

 private:
  cpp20::span<uint8_t> data_;
  uint64_t block_size_;
};

struct BlockDevice {
  std::vector<uint8_t> data;
  std::unique_ptr<FakeReader> reader;
  std::unique_ptr<FakeWriter> writer;
};

BlockDevice CreateBlockDevice(uint64_t block_count, uint64_t block_size) {
  BlockDevice device;
  device.data.resize(block_count * block_size, 0);
  device.reader = std::make_unique<FakeReader>(device.data, block_size);
  device.writer = std::make_unique<FakeWriter>(device.data, block_size);
  return device;
}

constexpr uint64_t kBlockCount = 200;
constexpr uint64_t kBlockSize = 64;

template <size_t N>
constexpr std::array<uint8_t, N> MakeData() {
  std::array<uint8_t, N> data = {};

  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = i % 256;
  }

  return data;
}

TEST(BlockWriterTest, UnalignedSingleBlockIsOk) {
  constexpr uint64_t kOffset = kBlockSize + 1;

  auto [data, reader, writer] = CreateBlockDevice(kBlockCount, kBlockSize);
  BlockWriter block_writer(kBlockSize, kBlockCount, std::move(reader), std::move(writer));

  // Unaligned fits in a single block.
  constexpr auto kData = MakeData<kBlockSize - 1>();

  // Canary values.
  data[kOffset - 1] = 15;
  data[kOffset + kData.size()] = 15;

  auto result = block_writer.Write(kOffset, kData);
  ASSERT_TRUE(result.is_ok()) << result.error();

  EXPECT_TRUE(memcmp(data.data() + kOffset, kData.data(), kData.size()) == 0);
  EXPECT_EQ(data[kOffset - 1], 15);
  EXPECT_EQ(data[kOffset + kData.size()], 15);
}

TEST(BlockWriterTest, UnalignedMultipleBlockIsOk) {
  constexpr uint64_t kOffset = kBlockSize + 1;

  auto [data, reader, writer] = CreateBlockDevice(kBlockCount, kBlockSize);
  BlockWriter block_writer(kBlockSize, kBlockCount, std::move(reader), std::move(writer));

  // Unaligned has an aligned block at the end.
  constexpr auto kData = MakeData<2 * kBlockSize - 1>();

  // Canary values.
  data[kOffset - 1] = 15;
  data[kOffset + kData.size()] = 15;

  auto result = block_writer.Write(kOffset, kData);
  ASSERT_TRUE(result.is_ok()) << result.error();

  EXPECT_TRUE(memcmp(data.data() + kOffset, kData.data(), kData.size()) == 0);
  EXPECT_EQ(data[kOffset - 1], 15);
  EXPECT_EQ(data[kOffset + kData.size()], 15);
}

TEST(BlockWriterTest, UnalignedMultipleBlockWithUnalignedEndIsOk) {
  constexpr uint64_t kOffset = kBlockSize + 1;

  auto [data, reader, writer] = CreateBlockDevice(kBlockCount, kBlockSize);
  BlockWriter block_writer(kBlockSize, kBlockCount, std::move(reader), std::move(writer));

  // Unaligned has an aligned block at the end and aligned block in the middle.
  constexpr auto kData = MakeData<3 * kBlockSize - 2>();

  // Canary values.
  data[kOffset - 1] = 15;
  data[kOffset + kData.size()] = 15;

  auto result = block_writer.Write(kOffset, kData);
  ASSERT_TRUE(result.is_ok()) << result.error();

  EXPECT_TRUE(memcmp(data.data() + kOffset, kData.data(), kData.size()) == 0);
  EXPECT_EQ(data[kOffset - 1], 15);
  EXPECT_EQ(data[kOffset + kData.size()], 15);
}

TEST(BlockWriterTest, AlignedBlockWithUnalignedEndIsOk) {
  constexpr uint64_t kOffset = kBlockSize;

  auto [data, reader, writer] = CreateBlockDevice(kBlockCount, kBlockSize);
  BlockWriter block_writer(kBlockSize, kBlockCount, std::move(reader), std::move(writer));

  // Starts with aligned block then has a trailing tail.
  constexpr auto kData = MakeData<2 * kBlockSize - 1>();

  // Canary values.
  data[kOffset - 1] = 15;
  data[kOffset + kData.size()] = 15;

  auto result = block_writer.Write(kOffset, kData);
  ASSERT_TRUE(result.is_ok()) << result.error();

  EXPECT_TRUE(memcmp(data.data() + kOffset, kData.data(), kData.size()) == 0);
  EXPECT_EQ(data[kOffset - 1], 15);
  EXPECT_EQ(data[kOffset + kData.size()], 15);
}

TEST(BlockWriterTest, AlignedBlockWithUnalignedEndAndAlignedMiddleIsOk) {
  constexpr uint64_t kOffset = kBlockSize;

  auto [data, reader, writer] = CreateBlockDevice(kBlockCount, kBlockSize);
  BlockWriter block_writer(kBlockSize, kBlockCount, std::move(reader), std::move(writer));

  // Starts with aligned block then has a trailing tail.
  constexpr auto kData = MakeData<3 * kBlockSize - 1>();

  // Canary values.
  data[kOffset - 1] = 15;
  data[kOffset + kData.size()] = 15;

  auto result = block_writer.Write(kOffset, kData);
  ASSERT_TRUE(result.is_ok()) << result.error();

  EXPECT_TRUE(memcmp(data.data() + kOffset, kData.data(), kData.size()) == 0);
  EXPECT_EQ(data[kOffset - 1], 15);
  EXPECT_EQ(data[kOffset + kData.size()], 15);
}

TEST(BlockWriterTest, FullyAlignedBlockRangeIsOk) {
  constexpr uint64_t kOffset = kBlockSize;

  auto [data, reader, writer] = CreateBlockDevice(kBlockCount, kBlockSize);
  BlockWriter block_writer(kBlockSize, kBlockCount, std::move(reader), std::move(writer));

  // Starts with aligned block then has a trailing tail.
  constexpr auto kData = MakeData<3 * kBlockSize>();

  // Canary values.
  data[kOffset - 1] = 15;
  data[kOffset + kData.size()] = 15;

  auto result = block_writer.Write(kOffset, kData);
  ASSERT_TRUE(result.is_ok()) << result.error();

  EXPECT_TRUE(memcmp(data.data() + kOffset, kData.data(), kData.size()) == 0);
  EXPECT_EQ(data[kOffset - 1], 15);
  EXPECT_EQ(data[kOffset + kData.size()], 15);
}

TEST(BlockWriterTest, OutOfRangeWriteIsError) {
  constexpr uint64_t kOffset = kBlockSize * kBlockCount;
  auto [data, reader, writer] = CreateBlockDevice(kBlockCount, kBlockSize);
  BlockWriter block_writer(kBlockSize, kBlockCount, std::move(reader), std::move(writer));

  constexpr auto kData = MakeData<1>();

  // The buffer size passes the end.
  auto result = block_writer.Write(kOffset, kData);
  ASSERT_TRUE(result.is_error());
}

}  // namespace
}  // namespace storage::volume_image
