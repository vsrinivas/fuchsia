// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fpromise/result.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/fvm_check.h"
#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_image_extend.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/fd_test_helper.h"
#include "src/storage/volume_image/utils/fd_writer.h"

namespace storage::volume_image {
namespace {

constexpr std::string_view kFvmSparseImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_fvm_small.sparse.blk";

fpromise::result<TempFile, std::string> CreateFvmBlockImage(
    std::optional<uint64_t> length = std::nullopt) {
  auto image_reader_or = FdReader::Create(kFvmSparseImagePath);
  if (image_reader_or.is_error()) {
    return image_reader_or.take_error_result();
  }
  auto image_reader = std::make_unique<FdReader>(image_reader_or.take_value());

  auto fvm_descriptor_or = FvmSparseReadImage(0, std::move(image_reader));
  if (fvm_descriptor_or.is_error()) {
    return fvm_descriptor_or.take_error_result();
  }
  auto fvm_descriptor = fvm_descriptor_or.take_value();
  if (length.has_value()) {
    FvmOptions options = fvm_descriptor.options();
    options.target_volume_size = length;
    auto tmp = FvmDescriptor::Builder(std::move(fvm_descriptor)).SetOptions(options).Build();
    if (tmp.is_error()) {
      return tmp.take_error_result();
    }
    fvm_descriptor = tmp.take_value();
  }

  auto block_image_or = TempFile::Create();
  if (block_image_or.is_error()) {
    return block_image_or.take_error_result();
  }
  auto block_image_writer_or = FdWriter::Create(block_image_or.value().path());
  if (block_image_writer_or.is_error()) {
    return block_image_writer_or.take_error_result();
  }

  if (length.has_value()) {
    if (truncate(block_image_or.value().path().data(),
                 fvm_descriptor.options().target_volume_size.value()) != 0) {
      return fpromise::error("Failed to truncate image to final size.");
    }
  }

  fvm_descriptor.WriteBlockImage(block_image_writer_or.value());
  return fpromise::ok(block_image_or.take_value());
}

constexpr uint64_t kTrimmedImagePartitionSize = 200 << 20;

fpromise::result<TempFile, std::string> CreateTrimmedFvmBlockImage() {
  auto image_or = CreateFvmBlockImage(kTrimmedImagePartitionSize);
  if (image_or.is_error()) {
    return image_or.take_error_result();
  }
  auto reader_or = FdReader::Create(image_or.value().path());
  if (reader_or.is_error()) {
    return reader_or.take_error_result();
  }

  auto size_or = FvmImageGetTrimmedSize(reader_or.value());
  if (size_or.is_error()) {
    return size_or.take_error_result();
  }

  if (truncate(image_or.value().path().data(), static_cast<off_t>(size_or.value())) != 0) {
    const auto* err = strerror(errno);
    return fpromise::error("Image truncation failed. More specifically: " + std::string(err) + ".");
  }
  return image_or;
}

void CheckFvm(std::string_view image_path, uint64_t expected_partition_size,
              uint64_t expected_image_size) {
  // FVM is valid.
  fbl::unique_fd fvm_fd(open(image_path.data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());
  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());

  // Check that the partition size is correct.
  auto reader_or = FdReader::Create(image_path);
  ASSERT_TRUE(reader_or.is_ok()) << reader_or.error();

  // Partition size.
  auto partition_size_or = FvmImageGetSize(reader_or.value());
  ASSERT_TRUE(partition_size_or.is_ok()) << partition_size_or.error();
  ASSERT_EQ(partition_size_or.value(), expected_partition_size);

  // Image size.
  struct stat fvm_stat = {};
  ASSERT_EQ(stat(image_path.data(), &fvm_stat), 0);
  ASSERT_EQ(static_cast<uint64_t>(fvm_stat.st_size), expected_image_size);
}

TEST(ExtendCommandTest, UpdatesFvmPartitionSizeAndIsValid) {
  ExtendParams params;

  auto fvm_image_or = CreateFvmBlockImage();
  ASSERT_TRUE(fvm_image_or.is_ok()) << fvm_image_or.error() << " " << kFvmSparseImagePath;
  auto fvm_image = fvm_image_or.take_value();
  params.image_path = fvm_image.path();

  auto reader_or = FdReader::Create(fvm_image.path());
  ASSERT_TRUE(reader_or.is_ok()) << reader_or.error();
  auto image_size_or = FvmImageGetSize(reader_or.value());
  ASSERT_TRUE(image_size_or.is_ok()) << image_size_or.error();

  // Pick a bigger size.
  params.length = 2 * image_size_or.value();

  ASSERT_TRUE(Extend(params).is_ok());
  CheckFvm(params.image_path, params.length.value(), params.length.value());
}

TEST(ExtendCommandTest, TrimPartitionSizeMatchesLengthAndImageSizeIsTrimSize) {
  ExtendParams params;

  auto fvm_image_or = CreateFvmBlockImage();
  ASSERT_TRUE(fvm_image_or.is_ok()) << fvm_image_or.error() << " " << kFvmSparseImagePath;
  auto fvm_image = fvm_image_or.take_value();
  params.image_path = fvm_image.path();
  params.should_trim = true;

  auto reader_or = FdReader::Create(fvm_image.path());
  ASSERT_TRUE(reader_or.is_ok()) << reader_or.error();
  auto image_size_or = FvmImageGetSize(reader_or.value());
  ASSERT_TRUE(image_size_or.is_ok()) << image_size_or.error();

  // Pick a bigger size.
  params.length = 2 * image_size_or.value();

  ASSERT_TRUE(Extend(params).is_ok());
  auto expected_image_size_or = FvmImageGetTrimmedSize(reader_or.value());
  ASSERT_TRUE(expected_image_size_or.is_ok()) << expected_image_size_or.error();
  CheckFvm(params.image_path, params.length.value(), expected_image_size_or.value());
}

TEST(ExtendCommandTest, FitWithSmallerLengthKeepsImageSize) {
  ExtendParams params;

  auto fvm_image_or = CreateTrimmedFvmBlockImage();
  ASSERT_TRUE(fvm_image_or.is_ok()) << fvm_image_or.error() << " " << kFvmSparseImagePath;
  auto fvm_image = fvm_image_or.take_value();
  params.image_path = fvm_image.path();
  params.should_use_max_partition_size = true;

  auto reader_or = FdReader::Create(fvm_image.path());
  ASSERT_TRUE(reader_or.is_ok()) << reader_or.error();
  auto image_size_or = FvmImageGetSize(reader_or.value());
  ASSERT_TRUE(image_size_or.is_ok()) << image_size_or.error();

  // A smaller length should default to to image_size.
  params.length = 0.5 * image_size_or.value();

  auto extend_or = Extend(params);
  ASSERT_TRUE(extend_or.is_ok()) << extend_or.error();
  auto expected_image_size_or = FvmImageGetSize(reader_or.value());
  ASSERT_TRUE(expected_image_size_or.is_ok()) << expected_image_size_or.error();
  CheckFvm(params.image_path, expected_image_size_or.value(), expected_image_size_or.value());
}

TEST(ExtendCommandTest, FitWithLargerLengthExtendsImage) {
  ExtendParams params;

  auto fvm_image_or = CreateFvmBlockImage();
  ASSERT_TRUE(fvm_image_or.is_ok()) << fvm_image_or.error() << " " << kFvmSparseImagePath;
  auto fvm_image = fvm_image_or.take_value();
  params.image_path = fvm_image.path();
  params.should_use_max_partition_size = true;

  auto reader_or = FdReader::Create(fvm_image.path());
  ASSERT_TRUE(reader_or.is_ok()) << reader_or.error();
  auto image_size_or = FvmImageGetSize(reader_or.value());
  ASSERT_TRUE(image_size_or.is_ok()) << image_size_or.error();

  // A larger length should be honored instead of the image size.
  params.length = 2 * image_size_or.value();

  auto extend_or = Extend(params);
  ASSERT_TRUE(extend_or.is_ok()) << extend_or.error();
  CheckFvm(params.image_path, params.length.value(), params.length.value());
}

TEST(ExtendCommandTest, FitAndTrimWithSmallerLengthKeepsImageSize) {
  ExtendParams params;

  auto fvm_image_or = CreateTrimmedFvmBlockImage();
  ASSERT_TRUE(fvm_image_or.is_ok()) << fvm_image_or.error() << " " << kFvmSparseImagePath;
  auto fvm_image = fvm_image_or.take_value();
  params.image_path = fvm_image.path();
  params.should_use_max_partition_size = true;
  params.should_trim = true;

  auto reader_or = FdReader::Create(fvm_image.path());
  ASSERT_TRUE(reader_or.is_ok()) << reader_or.error();
  auto image_size_or = FvmImageGetSize(reader_or.value());
  ASSERT_TRUE(image_size_or.is_ok()) << image_size_or.error();

  // A smaller length should default to to image_size.
  params.length = 0.5 * image_size_or.value();

  auto extend_or = Extend(params);
  ASSERT_TRUE(extend_or.is_ok()) << extend_or.error();
  auto expected_partition_size_or = FvmImageGetSize(reader_or.value());
  ASSERT_TRUE(expected_partition_size_or.is_ok()) << expected_partition_size_or.error();
  auto expected_image_size_or = FvmImageGetTrimmedSize(reader_or.value());
  ASSERT_TRUE(expected_image_size_or.is_ok()) << expected_image_size_or.error();
  CheckFvm(params.image_path, expected_partition_size_or.value(), expected_image_size_or.value());
}

}  // namespace
}  // namespace storage::volume_image
