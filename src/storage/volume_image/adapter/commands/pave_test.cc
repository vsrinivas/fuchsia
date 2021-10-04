// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/fvm_check.h"
#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/fd_test_helper.h"
#include "src/storage/volume_image/utils/fd_writer.h"

namespace storage::volume_image {
namespace {

constexpr uint64_t kImageSize = 150 * (1 << 20);
constexpr uint64_t kInitialImageSize = 5 * (1 << 20);

constexpr std::string_view kFvmSparseImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_fvm_small.sparse.blk";

PaveParams MakeParams() {
  PaveParams params;
  params.input_path = kFvmSparseImagePath;
  params.is_output_embedded = false;
  params.type = TargetType::kFile;
  return params;
}

TEST(PaveCommandTest, CompressedSparseImageIsOk) {
  auto output_or = TempFile::Create();
  ASSERT_TRUE(output_or.is_ok()) << output_or.error();
  // Truncate file to a size, since we are not providing length.
  ASSERT_EQ(truncate(output_or.value().path().data(), kInitialImageSize), 0) << strerror(errno);

  auto pave_params = MakeParams();
  pave_params.output_path = output_or.value().path();

  auto pave_result = Pave(pave_params);
  ASSERT_TRUE(pave_result.is_ok()) << pave_result.error();

  fbl::unique_fd fvm_fd(open(output_or.value().path().data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());

  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());
}

TEST(PaveCommandTest, CompressedSparseImageWithLengthIsOk) {
  auto output_or = TempFile::Create();
  ASSERT_TRUE(output_or.is_ok()) << output_or.error();

  auto pave_params = MakeParams();
  pave_params.output_path = output_or.value().path();
  pave_params.length = kInitialImageSize;

  auto pave_result = Pave(pave_params);
  ASSERT_TRUE(pave_result.is_ok()) << pave_result.error();

  fbl::unique_fd fvm_fd(open(output_or.value().path().data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());

  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());
}

TEST(PaveCommandTest, CompressedSparseImageToBlockDeviceIsOk) {
  auto output_or = TempFile::Create();
  ASSERT_TRUE(output_or.is_ok()) << output_or.error();

  // Truncate file to a size, since we are not providing length.
  ASSERT_EQ(truncate(output_or.value().path().data(), kInitialImageSize), 0) << strerror(errno);

  auto pave_params = MakeParams();
  pave_params.output_path = output_or.value().path();
  pave_params.type = TargetType::kBlockDevice;

  auto pave_result = Pave(pave_params);
  ASSERT_TRUE(pave_result.is_ok()) << pave_result.error();

  fbl::unique_fd fvm_fd(open(output_or.value().path().data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());

  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());
}

TEST(PaveCommandTest, CompressedSparseImageToBlockDeviceWithLengthIsOk) {
  auto output_or = TempFile::Create();
  ASSERT_TRUE(output_or.is_ok()) << output_or.error();

  auto pave_params = MakeParams();
  pave_params.output_path = output_or.value().path();
  pave_params.type = TargetType::kBlockDevice;
  pave_params.length = kInitialImageSize;

  auto pave_result = Pave(pave_params);
  ASSERT_TRUE(pave_result.is_ok()) << pave_result.error();

  fbl::unique_fd fvm_fd(open(output_or.value().path().data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());

  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());
}

TEST(PaveCommandTest, CreateEmbeddedFvmImageIsOk) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  auto pave_params = MakeParams();
  pave_params.output_path = output_file_or.value().path();
  pave_params.is_output_embedded = true;
  pave_params.offset = kInitialImageSize / 2;
  pave_params.length = kInitialImageSize;

  pave_params.fvm_options.target_volume_size = kInitialImageSize;
  pave_params.fvm_options.max_volume_size = kImageSize;
  pave_params.fvm_options.compression.schema = CompressionSchema::kNone;
  ASSERT_EQ(truncate(std::string(output_file_or.value().path()).c_str(), 2 * kInitialImageSize), 0);

  // Add poison values before the offset and after the length to verify afterwards.
  std::array<uint8_t, 10> canary = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  auto output_writer_or = FdWriter::Create(pave_params.output_path);
  ASSERT_TRUE(output_writer_or.is_ok());
  ASSERT_TRUE(output_writer_or.value().Write(pave_params.offset.value() - canary.size(), canary));
  ASSERT_TRUE(output_writer_or.value().Write(
      pave_params.offset.value() + pave_params.length.value(), canary));

  auto pave_result = Pave(pave_params);
  ASSERT_TRUE(pave_result.is_ok()) << pave_result.error();

  // Copy the range into a new file and do fvm check on it.
  // Also check that the beginning and end are zeroes.
  auto fvm_reader_or = FdReader::Create(output_file_or.value().path());
  ASSERT_TRUE(fvm_reader_or.is_ok()) << fvm_reader_or.error();
  std::unique_ptr<Reader> fvm_reader = std::make_unique<FdReader>(fvm_reader_or.take_value());

  // Check canaries
  std::array<uint8_t, 10> canary_buffer;
  ASSERT_TRUE(fvm_reader->Read(pave_params.offset.value() - canary.size(), canary_buffer));
  ASSERT_TRUE(memcmp(canary_buffer.data(), canary.data(), canary.size()) == 0);

  ASSERT_TRUE(
      fvm_reader->Read(pave_params.offset.value() + pave_params.length.value(), canary_buffer));
  ASSERT_TRUE(memcmp(canary_buffer.data(), canary.data(), canary.size()) == 0);

  uint64_t current_offset = pave_params.offset.value();
  std::vector<uint8_t> buffer;
  buffer.resize(1 << 10, 0);

  auto copy_file_or = TempFile::Create();
  ASSERT_TRUE(copy_file_or.is_ok());

  auto fvm_copy_or = FdWriter::Create(copy_file_or.value().path());
  ASSERT_TRUE(fvm_copy_or.is_ok()) << fvm_copy_or.error();
  std::unique_ptr<FdWriter> fvm_copy = std::make_unique<FdWriter>(fvm_copy_or.take_value());

  // Copy contents into a new file to run fvm check.
  while (current_offset < pave_params.offset.value() + pave_params.length.value()) {
    auto buffer_view = cpp20::span<uint8_t>(buffer).subspan(
        0, std::min(pave_params.offset.value() + pave_params.length.value() - current_offset,
                    static_cast<uint64_t>(buffer.size())));
    ;
    ASSERT_TRUE(fvm_reader->Read(current_offset, buffer_view).is_ok());

    ASSERT_TRUE(fvm_copy->Write(current_offset - pave_params.offset.value(), buffer_view).is_ok());
    current_offset += buffer_view.size();
  }

  fbl::unique_fd fvm_fd(open(copy_file_or.value().path().data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());

  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());
}

// To run this test locally on a linux machine:
//  * sudo modprobe nandsim id_bytes=0x2c,0xdc,0x90,0xa6,0x54,0x0 badblocks=5
//  * chmod u=rw,og=rw /dev/mtd0
// chmod is required so fx test may run the test for you.
constexpr const char* kTestMtdDevicePath = "/dev/mtd0";

TEST(PaveCommandTest, MtdWriterIsOk) {
  // check that the mtd device and the block device are both present.
  fbl::unique_fd mtd_fd(open(kTestMtdDevicePath, O_RDWR));

  if (!mtd_fd.is_valid()) {
    GTEST_SKIP() << "No MTD device availble. " << strerror(errno);
  }

  auto pave_params = MakeParams();
  pave_params.output_path = kTestMtdDevicePath;
  pave_params.type = TargetType::kMtd;
  pave_params.is_output_embedded = true;
  pave_params.offset = 0;
  pave_params.max_bad_blocks = 41;
  pave_params.length = kInitialImageSize;

  pave_params.fvm_options.max_volume_size = 2 * kInitialImageSize;
  pave_params.fvm_options.compression.schema = CompressionSchema::kNone;

  auto pave_result = Pave(pave_params);
  ASSERT_TRUE(pave_result.is_ok()) << pave_result.error();
}

}  // namespace
}  // namespace storage::volume_image
