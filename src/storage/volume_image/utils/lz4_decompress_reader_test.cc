// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/lz4_decompress_reader.h"

#include <lib/stdcompat/span.h>
#include <sys/types.h>

#include <cstdint>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/fvm_sparse.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/fd_test_helper.h"
#include "src/storage/volume_image/utils/fd_writer.h"
#include "src/storage/volume_image/utils/lz4_compressor.h"
#include "src/storage/volume_image/utils/lz4_decompressor.h"

namespace storage::volume_image {
namespace {
// Path to a compressed sparse image.
constexpr std::string_view kFvmSparseImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_fvm.sparse.blk";

constexpr std::string_view kLoremIpsum =
    R"(Lorem ipsum dolor sit amet, consectetur adipiscing elit,
    sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
    Elit pellentesque habitant morbi tristique senectus et netus et. Blandit
    aliquam etiam erat velit scelerisque in. Placerat orci nulla pellentesque
    dignissim enim sit amet. Suspendisse ultrices gravida dictum fusce ut placerat
    orci. Pretium aenean pharetra magna ac placerat vestibulum lectus mauris ultrices.
    Nibh venenatis cras sed felis eget velit aliquet sagittis. Risus quis varius quam
    quisque id diam vel. Sed enim ut sem viverra. Fusce id velit ut tortor pretium.
    Amet dictum sit amet justo donec enim diam vulputate ut. Faucibus scelerisque eleifend
    donec pretium vulputate sapien nec. Curabitur gravida arcu ac tortor dignissim
    convallis aenean. Morbi non arcu risus quis varius quam quisque. Vitae suscipit
    tellus mauris a diam maecenas. Mattis enim ut tellus elementum sagittis vitae et leo
    duis. Lacinia quis vel eros donec ac odio.
    
    Feugiat in ante metus dictum at. Amet nisl suscipit adipiscing bibendum est.
    Bibendum ut tristique et egestas quis ipsum suspendisse ultrices. Sed euismod nisi
    porta lorem mollis aliquam ut porttitor leo. Libero id faucibus nisl tincidunt eget.
    Gravida in fermentum et sollicitudin ac orci. Accumsan sit amet nulla facilisi morbi
    tempus. Sed euismod nisi porta lorem mollis aliquam ut. Sed velit dignissim sodales
    ut eu sem integer. Purus in massa tempor nec feugiat nisl pretium. Eros in cursus
    turpis massa.
    
    A diam maecenas sed enim ut. Leo in vitae turpis massa sed. Lobortis scelerisque
    fermentum dui faucibus in ornare. Nullam eget felis eget nunc lobortis mattis. A cras
    semper auctor neque vitae tempus. Dignissim suspendisse in est ante in nibh mauris
    cursus. Dictumst quisque sagittis purus sit amet volutpat consequat mauris nunc. Vel
    quam elementum pulvinar etiam non quam lacus suspendisse faucibus. Libero just
    laoreet sit amet cursus sit amet. Imperdiet dui accumsan sit amet nulla. Platea
    dictumst quisque sagittis purus. Lobortis mattis aliquam faucibus purus in massa. Nec
    sagittis aliquam malesuada bibendum. Eu sem integer vitae justo. Sit amet dictum sit
    amet justo donec enim. Aliquet sagittis id consectetur purus ut faucibus pulvinar
    elementum integer. Diam vulputate ut pharetra sit amet aliquam. At consectetur lorem
    donec massa sapien faucibus et.)";

fpromise::result<std::vector<uint8_t>, std::string> CompressedData(
    cpp20::span<const uint8_t> source_data) {
  std::vector<uint8_t> compressed_data;
  Lz4Compressor compressor;
  if (auto result =
          compressor.Prepare([&compressed_data](cpp20::span<const uint8_t> compressed_chunk) {
            compressed_data.insert(compressed_data.end(), compressed_chunk.begin(),
                                   compressed_chunk.end());
            return fpromise::ok();
          });
      result.is_error()) {
    return result.take_error_result();
  }

  if (auto result = compressor.Compress(source_data); result.is_error()) {
    return result.take_error_result();
  }

  if (auto result = compressor.Finalize(); result.is_error()) {
    return result.take_error_result();
  }

  return fpromise::ok(compressed_data);
}

// Compressed Reader.
class FakeReader : public Reader {
 public:
  FakeReader(std::vector<uint8_t> data) : data_(data) {}

  uint64_t length() const final { return data_.size(); }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (buffer.empty()) {
      return fpromise::ok();
    }
    if (offset + buffer.size() > data_.size()) {
      return fpromise::error("FakeReader::Read out of bounds.");
    }
    memcpy(buffer.data(), data_.data() + offset, buffer.size());
    return fpromise::ok();
  }

 private:
  std::vector<uint8_t> data_;
};

constexpr uint64_t kUncompressedDataPrefix = 128;
constexpr uint64_t kDecompressedLength = kUncompressedDataPrefix + kLoremIpsum.size();
constexpr uint64_t kMaxBufferLength = kUncompressedDataPrefix + 1;
// constexpr uint64_t kMaxReadBufferLength = kUncompressedDataPrefix / 3;

fpromise::result<std::vector<uint8_t>, std::string> GetData() {
  auto data_or = CompressedData(cpp20::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(kLoremIpsum.data()), kLoremIpsum.size()));
  if (data_or.is_error()) {
    return data_or.take_error_result();
  }
  auto data = data_or.take_value();

  data.insert(data.begin(), kLoremIpsum.begin(), kLoremIpsum.begin() + kUncompressedDataPrefix);
  return fpromise::ok(data);
}

void CheckRangeMatch(uint64_t offset, const Reader& reader,
                     cpp20::span<const uint8_t> expected_data) {
  uint64_t bytes_to_read = reader.length() - offset;
  if (bytes_to_read > expected_data.size()) {
    bytes_to_read = expected_data.size();
  }

  std::vector<uint8_t> data;
  data.resize(bytes_to_read, 0);

  auto result = reader.Read(offset, data);
  ASSERT_TRUE(result.is_ok()) << result.error();

  EXPECT_TRUE(memcmp(data.data(), expected_data.data(), bytes_to_read) == 0);

  if (data.empty()) {
    return;
  }
}

TEST(Lz4DecompressReaderTest, ReadingUncompressedAreaIsOk) {
  auto data_or = GetData();
  ASSERT_TRUE(data_or.is_ok()) << data_or.error();
  auto data = data_or.take_value();

  std::shared_ptr<FakeReader> compressed_reader = std::make_shared<FakeReader>(data);
  Lz4DecompressReader decompressed_reader(kUncompressedDataPrefix, kDecompressedLength,
                                          compressed_reader);
  auto init_result = decompressed_reader.Initialize(kMaxBufferLength);
  ASSERT_TRUE(init_result.is_ok()) << init_result.error();

  auto view = cpp20::span<uint8_t>(data);

  // Read part of uncompressed data only.
  ASSERT_NO_FATAL_FAILURE(
      CheckRangeMatch(0, decompressed_reader, view.subspan(0, kUncompressedDataPrefix / 4)));

  // The entire uncompressed data.
  ASSERT_NO_FATAL_FAILURE(
      CheckRangeMatch(0, decompressed_reader, view.subspan(0, kUncompressedDataPrefix)));
}

TEST(Lz4DecompressReaderTest, ReadingCompressedAreaIsOk) {
  auto data_or = GetData();
  ASSERT_TRUE(data_or.is_ok()) << data_or.error();
  auto data = data_or.take_value();

  std::shared_ptr<FakeReader> compressed_reader = std::make_shared<FakeReader>(data);
  Lz4DecompressReader decompressed_reader(kUncompressedDataPrefix, kDecompressedLength,
                                          compressed_reader);
  auto init_result = decompressed_reader.Initialize(kMaxBufferLength);
  ASSERT_TRUE(init_result.is_ok()) << init_result.error();

  auto lorem_ipsum = cpp20::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(kLoremIpsum.data()), kLoremIpsum.size());

  // Random chunk.
  ASSERT_NO_FATAL_FAILURE(CheckRangeMatch(kUncompressedDataPrefix + 500, decompressed_reader,
                                          lorem_ipsum.subspan(500)));

  // Read part of uncompressed data only.
  ASSERT_NO_FATAL_FAILURE(
      CheckRangeMatch(kUncompressedDataPrefix, decompressed_reader, lorem_ipsum.subspan(0, 1)));

  // The entire uncompressed data.
  ASSERT_NO_FATAL_FAILURE(
      CheckRangeMatch(kUncompressedDataPrefix, decompressed_reader, lorem_ipsum));
}

TEST(Lz4DecompressReaderTest, ReadingBothAreasIsOk) {
  auto data_or = GetData();
  ASSERT_TRUE(data_or.is_ok()) << data_or.error();
  auto data = data_or.take_value();

  std::shared_ptr<FakeReader> compressed_reader = std::make_shared<FakeReader>(data);
  Lz4DecompressReader decompressed_reader(kUncompressedDataPrefix, kDecompressedLength,
                                          compressed_reader);
  auto init_result = decompressed_reader.Initialize(kMaxBufferLength);
  ASSERT_TRUE(init_result.is_ok()) << init_result.error();

  std::array<uint8_t, 2> expected_data = {data[kUncompressedDataPrefix - 1], kLoremIpsum[0]};

  // The entire uncompressed data.
  ASSERT_NO_FATAL_FAILURE(
      CheckRangeMatch(kUncompressedDataPrefix - 1, decompressed_reader, expected_data));
}

TEST(Lz4DecompressReaderTest, DecompressingSparseFvmIsOk) {
  auto decompressed_image_or = TempFile::Create();
  ASSERT_TRUE(decompressed_image_or.is_ok()) << decompressed_image_or.error();
  auto decompressed_image = decompressed_image_or.take_value();

  auto compressed_reader_or = FdReader::Create(kFvmSparseImagePath);
  ASSERT_TRUE(compressed_reader_or.is_ok()) << compressed_reader_or.error();
  auto compressed_reader = compressed_reader_or.take_value();

  auto decompressed_writer_or = FdWriter::Create(decompressed_image.path());
  ASSERT_TRUE(decompressed_writer_or.is_ok()) << decompressed_writer_or.error();
  auto decompressed_writer = decompressed_writer_or.take_value();

  auto decompress_result = FvmSparseDecompressImage(0, compressed_reader, decompressed_writer);
  ASSERT_TRUE(decompress_result.is_ok()) << decompress_result.error();
  ASSERT_TRUE(decompress_result.value());

  // Read the header.
  fvm::SparseImage header;
  cpp20::span<uint8_t> header_buffer(reinterpret_cast<uint8_t*>(&header), sizeof(header));
  auto header_read_result = compressed_reader.Read(0, header_buffer);
  ASSERT_TRUE(header_read_result.is_ok()) << header_read_result.error();

  uint64_t compressed_data_offset = header.header_length;

  auto expected_decompressed_reader_or = FdReader::Create(decompressed_image.path());
  ASSERT_TRUE(expected_decompressed_reader_or.is_ok()) << expected_decompressed_reader_or.error();
  auto expected_decompressed_reader = expected_decompressed_reader_or.take_value();

  std::shared_ptr<Reader> shared_compressed_reader =
      std::make_shared<FdReader>(std::move(compressed_reader));
  // For a fvm sparse image, we can either decompress and calculate the length in a single pass,
  // or  calculate the expected uncompressed size based on the accumulated extent length.
  Lz4DecompressReader decompressed_reader(
      compressed_data_offset, expected_decompressed_reader.length(), shared_compressed_reader);
  ASSERT_TRUE(decompressed_reader.Initialize().is_ok());

  // Now compare offsets.
  constexpr uint64_t kDecompressedBufferSize = 64u << 10;
  std::vector<uint8_t> actual_decompressed_buffer;
  actual_decompressed_buffer.resize(kDecompressedBufferSize, 0);

  std::vector<uint8_t> expected_decompressed_buffer;
  expected_decompressed_buffer.resize(kDecompressedBufferSize, 0);

  // We skip the header itself, since some flags might be different, from the compressed and the non
  // compressed. Though this sectionis not compressed.
  uint64_t read_bytes = sizeof(fvm::SparseImage);
  while (read_bytes < decompressed_reader.length()) {
    uint64_t bytes_to_read = kDecompressedBufferSize;
    if (bytes_to_read > decompressed_reader.length() - read_bytes) {
      bytes_to_read = decompressed_reader.length() - read_bytes;
    }
    auto actual_decompressed_view =
        cpp20::span<uint8_t>(actual_decompressed_buffer).subspan(0, bytes_to_read);
    auto read_result = decompressed_reader.Read(read_bytes, actual_decompressed_view);
    ASSERT_TRUE(read_result.is_ok()) << read_result.error();

    auto expected_decompressed_view =
        cpp20::span<uint8_t>(expected_decompressed_buffer).subspan(0, bytes_to_read);
    auto expected_read_result =
        expected_decompressed_reader.Read(read_bytes, expected_decompressed_view);
    ASSERT_TRUE(expected_read_result.is_ok()) << expected_read_result.error();

    EXPECT_TRUE(memcmp(actual_decompressed_view.data(), expected_decompressed_view.data(),
                       actual_decompressed_view.size()) == 0)
        << " offset " << read_bytes << " size " << bytes_to_read;
    read_bytes += bytes_to_read;
  }

  // Check that read_bytes contain all data from the decompressed image.
  EXPECT_EQ(read_bytes, expected_decompressed_reader.length());
}

}  // namespace
}  // namespace storage::volume_image
