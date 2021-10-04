// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/adapter/mtd_writer.h"

#include <fcntl.h>
#include <lib/fpromise/result.h>

#include <string>
#include <string_view>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/ftl/ftl_io.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/fd_test_helper.h"
#include "src/storage/volume_image/utils/fd_writer.h"

namespace storage::volume_image {
namespace {

// To run this test locally on a linux machine:
//  * sudo modprobe nandsim id_bytes=0x2c,0xdc,0x90,0xa6,0x54,0x0 badblocks=5
//  * chmod u=rw,og=rw /dev/mtd0
// chmod is required so fx test may run the test for you.
constexpr const char* kTestMtdDevicePath = "/dev/mtd0";
constexpr std::string_view kFvmSparseImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_fvm.sparse.blk";

fpromise::result<void, std::string> ReadUnalignedBlock(uint64_t offset, cpp20::span<uint8_t> data,
                                                       cpp20::span<uint8_t> block_buffer,
                                                       Reader& reader) {
  uint64_t read_bytes = 0;

  while (read_bytes < data.size()) {
    uint64_t current_offset = offset + read_bytes;
    uint64_t block_offset =
        GetBlockFromBytes(current_offset, block_buffer.size()) * block_buffer.size();
    if (auto result = reader.Read(block_offset, block_buffer); result.is_error()) {
      return result;
    }
    uint64_t bytes_in_buffer = block_offset + block_buffer.size() - current_offset;
    if (bytes_in_buffer > data.size() - read_bytes) {
      bytes_in_buffer = data.size() - read_bytes;
    }
    memcpy(data.data() + read_bytes, block_buffer.data(), bytes_in_buffer);
    read_bytes += bytes_in_buffer;
  }
  return fpromise::ok();
}

TEST(MtdWriterTest, WriteContentsAreOk) {
  // check that the mtd device and the block device are both present.
  fbl::unique_fd mtd_fd(open(kTestMtdDevicePath, O_RDWR));

  if (!mtd_fd.is_valid()) {
    GTEST_SKIP() << "No MTD device availble. " << strerror(errno);
  }
  MtdParams params;
  params.offset = 0;
  params.max_bad_blocks = 5;
  FtlHandle handle;
  auto mtd_writer_or = CreateMtdWriter(kTestMtdDevicePath, params, &handle);
  ASSERT_TRUE(mtd_writer_or.is_ok()) << mtd_writer_or.error();

  ASSERT_TRUE(mtd_writer_or.is_ok()) << mtd_writer_or.error();
  auto mtd_writer = mtd_writer_or.take_value();

  std::vector<uint8_t> data = {1, 12, 123};
  std::vector<uint8_t> actual_data;
  actual_data.resize(data.size());

  // This must be initialized after the previous write, so the FTL can be initialized from disk.
  auto reader = handle.MakeReader();

  std::vector<uint8_t> block_buffer;
  block_buffer.resize(handle.instance().page_size(), 0);

  {
    auto write_result = mtd_writer->Write(0, data);
    ASSERT_TRUE(write_result.is_ok()) << write_result.error();

    auto read_result = ReadUnalignedBlock(0, actual_data, block_buffer, *reader);
    ASSERT_TRUE(read_result.is_ok()) << read_result.error();

    EXPECT_THAT(actual_data, testing::ElementsAreArray(data));
  }

  {
    uint64_t offset = handle.instance().page_size() - 1;
    auto write_result = mtd_writer->Write(offset, data);
    ASSERT_TRUE(write_result.is_ok()) << write_result.error();
    memset(block_buffer.data(), 0, block_buffer.size());
    auto read_result = ReadUnalignedBlock(offset, actual_data, block_buffer, *reader);
    ASSERT_TRUE(read_result.is_ok()) << read_result.error();

    EXPECT_THAT(actual_data, testing::ElementsAreArray(data));
  }

  {
    uint64_t offset = handle.instance().page_size();
    data.resize(2 * handle.instance().page_size());
    for (size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<uint8_t>((i + 3) % 256);
    }
    actual_data.resize(data.size());

    auto write_result = mtd_writer->Write(offset, data);
    ASSERT_TRUE(write_result.is_ok()) << write_result.error();
    memset(block_buffer.data(), 0, block_buffer.size());
    auto read_result = reader->Read(offset, actual_data);
    ASSERT_TRUE(read_result.is_ok()) << read_result.error();

    EXPECT_TRUE(memcmp(actual_data.data(), data.data(), data.size()) == 0);
  }
}

TEST(MtdWriterTest, WriteFvmAndPersistsIsOk) {
  // check that the mtd device and the block device are both present.
  fbl::unique_fd mtd_fd(open(kTestMtdDevicePath, O_RDWR));

  if (!mtd_fd.is_valid()) {
    GTEST_SKIP() << "No MTD device availble. " << strerror(errno);
  }

  // Read the compressed sparse image.
  auto compressed_sparse_reader_or = FdReader::Create(kFvmSparseImagePath);
  ASSERT_TRUE(compressed_sparse_reader_or.is_ok()) << compressed_sparse_reader_or.error();
  auto compressed_sparse_reader =
      std::make_unique<FdReader>(compressed_sparse_reader_or.take_value());

  auto fvm_descriptor_or = FvmSparseReadImage(0, std::move(compressed_sparse_reader));
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  MtdParams params;
  params.offset = 0;
  params.max_bad_blocks = 5;
  params.format = true;
  {  // So the handle and the writer are destroyed, thus contents should be flushed.
    FtlHandle handle;
    auto mtd_writer_or = CreateMtdWriter(kTestMtdDevicePath, params, &handle);
    ASSERT_TRUE(mtd_writer_or.is_ok()) << mtd_writer_or.error();

    // Write the contents to the mtd device.
    auto write_result = fvm_descriptor.WriteBlockImage(*mtd_writer_or.value());
    ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  }

  // Obtain a new FTL instance, this should verify that the FTL is persisted.
  FtlHandle handle;
  params.format = false;
  auto mtd_writer_or = CreateMtdWriter(kTestMtdDevicePath, params, &handle);
  ASSERT_TRUE(mtd_writer_or.is_ok()) << mtd_writer_or.error();

  // Write the contents into a file and compare each pair of blocks.
  auto expected_image_or = TempFile::Create();
  ASSERT_TRUE(expected_image_or.is_ok()) << expected_image_or.error();
  auto expected_image = expected_image_or.take_value();

  auto expected_image_writer_or = FdWriter::Create(expected_image.path());
  ASSERT_TRUE(expected_image_writer_or.is_ok()) << expected_image_writer_or.error();
  auto expected_image_writer = expected_image_writer_or.take_value();

  auto expected_write_result = fvm_descriptor.WriteBlockImage(expected_image_writer);
  ASSERT_TRUE(expected_write_result.is_ok()) << expected_write_result.error();

  auto expected_image_reader_or = FdReader::Create(expected_image.path());
  ASSERT_TRUE(expected_image_reader_or.is_ok()) << expected_image_reader_or.error();
  auto expected_image_reader = expected_image_reader_or.take_value();

  uint64_t read_bytes = 0;
  std::vector<uint8_t> buffer;
  buffer.resize(handle.instance().page_size());

  std::vector<uint8_t> actual_buffer;
  actual_buffer.resize(handle.instance().page_size());

  std::vector<uint8_t> page_buffer;
  page_buffer.resize(handle.instance().page_size());

  auto actual_reader = handle.MakeReader();

  // RawNand and Files treat unwritten ranges differently. While a file will zero fill the skipped
  // range, raw nand sees 0xFF or better said, a cleanly formatted FTL, with an unwritten, which
  // mean unmapped block device block(nand device page) will assume 0xFF.
  // The values below were extracted from the fvm metadata.
  std::vector<uint8_t> empty_slices = {29, 31};
  std::vector<uint8_t> empty_page;
  empty_page.resize(handle.instance().page_size(), 0xFF);

  // Read back the fvm header to calculate the slice offsets.
  fvm::Header header = {};
  ASSERT_TRUE(ReadUnalignedBlock(
                  0, cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&header), sizeof(header)),
                  page_buffer, *actual_reader)
                  .is_ok());

  while (read_bytes < expected_image_reader.length()) {
    uint64_t bytes_to_read = buffer.size();
    if (bytes_to_read > expected_image_reader.length() - read_bytes) {
      bytes_to_read = expected_image_reader.length() - read_bytes;
    }
    auto expected_result = expected_image_reader.Read(read_bytes, buffer);
    ASSERT_TRUE(expected_result.is_ok()) << expected_result.error();

    // Read same range from the FTL.
    auto actual_result = ReadUnalignedBlock(
        read_bytes, cpp20::span<uint8_t>(actual_buffer).subspan(0, bytes_to_read), page_buffer,
        *actual_reader);
    ASSERT_TRUE(actual_result.is_ok()) << actual_result.error();

    bool is_empty = false;
    for (auto slice : empty_slices) {
      if (read_bytes >= header.GetSliceDataOffset(slice) &&
          read_bytes + bytes_to_read <= header.GetSliceDataOffset(slice + 1)) {
        is_empty = true;
        EXPECT_TRUE(memcmp(actual_buffer.data(), empty_page.data(), bytes_to_read) == 0);
        break;
      }
    }

    if (!is_empty) {
      EXPECT_TRUE(memcmp(buffer.data(), actual_buffer.data(), bytes_to_read) == 0);
    }
    read_bytes += bytes_to_read;
  }
}

}  // namespace
}  // namespace storage::volume_image
