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

#include "src/storage/fvm/format.h"
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

struct FvmInfo {
  uint64_t slice_size;
  uint64_t slice_count;
};

fpromise::result<FvmInfo, std::string> GetFvmInfo() {
  auto image_reader_or = FdReader::Create(kFvmSparseImagePath);
  if (image_reader_or.is_error()) {
    return image_reader_or.take_error_result();
  }

  auto fvm_descriptor_or =
      FvmSparseReadImage(0, std::make_unique<FdReader>(image_reader_or.take_value()));
  if (fvm_descriptor_or.is_error()) {
    return fvm_descriptor_or.take_error_result();
  }

  return fpromise::ok(FvmInfo{.slice_size = fvm_descriptor_or.value().options().slice_size,
                              .slice_count = fvm_descriptor_or.value().slice_count()});
}

TEST(SizeCommandTest, ReturnsCorrectSize) {
  SizeParams params;
  params.image_path = kFvmSparseImagePath;

  auto size_or = Size(params);
  ASSERT_TRUE(size_or.is_ok()) << size_or.error();

  auto fvm_info_or = GetFvmInfo();
  ASSERT_TRUE(fvm_info_or.is_ok()) << fvm_info_or.error();

  auto header = fvm::Header::FromSliceCount(
      fvm::kMaxUsablePartitions, fvm_info_or.value().slice_count, fvm_info_or.value().slice_size);
  EXPECT_EQ(size_or.value(), header.fvm_partition_size);
}

TEST(SizeCommandTest, WithLengthGreaterThanSizeReturnsNullopt) {
  auto fvm_info_or = GetFvmInfo();
  ASSERT_TRUE(fvm_info_or.is_ok()) << fvm_info_or.error();

  auto header = fvm::Header::FromSliceCount(
      fvm::kMaxUsablePartitions, fvm_info_or.value().slice_count, fvm_info_or.value().slice_size);

  SizeParams params;
  params.image_path = kFvmSparseImagePath;
  params.length = header.fvm_partition_size;

  {
    auto size_or = Size(params);
    ASSERT_TRUE(size_or.is_ok()) << size_or.error();
    EXPECT_EQ(size_or.value(), header.fvm_partition_size);
  }

  {
    params.length = header.fvm_partition_size + 1;

    auto size_or = Size(params);
    ASSERT_TRUE(size_or.is_ok()) << size_or.error();
    EXPECT_EQ(size_or.value(), header.fvm_partition_size);
  }
}

TEST(SizeCommandTest, WithLengthSmallerThanSizeReturnsError) {
  auto fvm_info_or = GetFvmInfo();
  ASSERT_TRUE(fvm_info_or.is_ok()) << fvm_info_or.error();

  auto header = fvm::Header::FromSliceCount(
      fvm::kMaxUsablePartitions, fvm_info_or.value().slice_count, fvm_info_or.value().slice_size);

  SizeParams params;
  params.image_path = kFvmSparseImagePath;
  params.length = header.fvm_partition_size - 1;

  auto size_or = Size(params);
  ASSERT_TRUE(size_or.is_error());
}

}  // namespace
}  // namespace storage::volume_image
