// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "lib/zx/time.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/fvm/format.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"
#include "src/storage/volume_image/adapter/blobfs_partition.h"
#include "src/storage/volume_image/adapter/minfs_partition.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/guid.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace {

constexpr std::string_view kBlobfsImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_blobfs.blk";

constexpr std::string_view kMinfsImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_minfs.blk";

// Implementation of a Writer backed by a VMO.
class VmoWriter final : public Writer {
 public:
  VmoWriter(zx::unowned_vmo vmo, uint64_t size) : vmo_(vmo), vmo_size_(size) {}

  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> buffer) final {
    if (offset + buffer.size() > vmo_size_) {
      auto result = zx::make_status(vmo_->set_size(offset + buffer.size()));
      if (result.is_error()) {
        return fit::error(std::string("VmoWriter::Write failed to extend vmo with status: ") +
                          result.status_string() + ".");
      }
      vmo_size_ = offset + buffer.size();
    }
    auto result = zx::make_status(vmo_->write(buffer.data(), offset, buffer.size()));
    if (result.is_error()) {
      return fit::error(std::string("VmoWriter::Write failed to write to vmo with status: ") +
                        result.status_string() + ".");
    }
    return fit::ok();
  }

  uint64_t vmo_size() const { return vmo_size_; }

 private:
  zx::unowned_vmo vmo_;
  uint64_t vmo_size_;
};

FvmOptions MakeFvmOptions(uint64_t slice_size) {
  FvmOptions options;
  options.slice_size = slice_size;
  return options;
}

constexpr uint64_t kSliceSize = 32u * (1u << 10);

// The test will write the fvm image into a vmo, and then bring up a fvm driver,
// on top a ramdisk with the written data. The blobfs partition in the fvm driver,
// should pass FSCK if everything is correct.
TEST(AdapterTest, BlobfsPartitonInFvmImageFromFvmDescriptorPassesFsck) {
  constexpr uint64_t kImageSize = 500u * (1u << 20);
  auto fvm_options = MakeFvmOptions(kSliceSize);
  // 500 MB fvm image.
  fvm_options.target_volume_size = kImageSize;
  PartitionOptions partition_options;

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto fvm_descriptor_or =
      FvmDescriptor::Builder().SetOptions(fvm_options).AddPartition(std::move(partition)).Build();
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  zx::vmo fvm_vmo;
  ASSERT_EQ(zx::vmo::create(fvm_options.target_volume_size.value(), 0u, &fvm_vmo), ZX_OK);
  VmoWriter fvm_writer(fvm_vmo.borrow(), kImageSize);

  auto write_result = fvm_descriptor.WriteBlockImage(fvm_writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  // Extent the fvm vmo to next block boundary of the ramdisk.
  constexpr uint64_t kBlockSize = 512;
  uint64_t block_count = GetBlockCount(0, fvm_writer.vmo_size(), kBlockSize);
  if (fvm_writer.vmo_size() % kBlockSize != 0) {
    ASSERT_EQ(fvm_vmo.set_size(kBlockSize * block_count), ZX_OK);
  }

  // Create ramdisk thingy.
  // duplicate fvm vmo.
  zx::vmo ramdisk_vmo;
  ASSERT_EQ(fvm_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &ramdisk_vmo), ZX_OK);
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(ramdisk_vmo), kBlockSize);
  ASSERT_TRUE(ramdisk_or.is_ok()) << ramdisk_or.status_string();
  auto ramdisk = std::move(ramdisk_or.value());

  int fvm_dev_fd = ramdisk_get_block_fd(ramdisk.client());
  auto fvm_bind_result = BindFvm(fvm_dev_fd);
  ASSERT_TRUE(fvm_bind_result.is_ok()) << fvm_bind_result.status_string();

  const auto& blobfs_partition = *fvm_descriptor.partitions().begin();
  std::array<char, PATH_MAX> partition_path = {};
  fbl::unique_fd blobfs_fd(open_partition(nullptr, blobfs_partition.volume().type.data(),
                                          zx::sec(10).get(), partition_path.data()));
  ASSERT_TRUE(blobfs_fd.is_valid());

  auto fsck_options = default_fsck_options;
  fsck_options.always_modify = false;
  fsck_options.never_modify = true;
  fsck_options.verbose = true;
  fsck_options.force = true;
  ASSERT_EQ(fsck(partition_path.data(), DISK_FORMAT_BLOBFS, &fsck_options, &launch_stdio_sync),
            ZX_OK);
}

// The test will write the fvm image into a vmo, and then bring up a fvm driver,
// on top a ramdisk with the written data. The blobfs partition in the fvm driver,
// should pass FSCK if everything is correct.
TEST(AdapterTest, MinfsPartitonInFvmImageFromFvmDescriptorPassesFsck) {
  constexpr uint64_t kImageSize = 500u * (1u << 20);
  auto fvm_options = MakeFvmOptions(kSliceSize);
  // 500 MB fvm image.
  fvm_options.target_volume_size = kImageSize;
  PartitionOptions partition_options;

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto fvm_descriptor_or =
      FvmDescriptor::Builder().SetOptions(fvm_options).AddPartition(std::move(partition)).Build();
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  zx::vmo fvm_vmo;
  ASSERT_EQ(zx::vmo::create(fvm_options.target_volume_size.value(), 0u, &fvm_vmo), ZX_OK);
  VmoWriter fvm_writer(fvm_vmo.borrow(), kImageSize);

  auto write_result = fvm_descriptor.WriteBlockImage(fvm_writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  // Extent the fvm vmo to next block boundary of the ramdisk.
  constexpr uint64_t kBlockSize = 512;
  uint64_t block_count = GetBlockCount(0, fvm_writer.vmo_size(), kBlockSize);
  if (fvm_writer.vmo_size() % kBlockSize != 0) {
    ASSERT_EQ(fvm_vmo.set_size(kBlockSize * block_count), ZX_OK);
  }

  // Create ramdisk thingy.
  // duplicate fvm vmo.
  zx::vmo ramdisk_vmo;
  ASSERT_EQ(fvm_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &ramdisk_vmo), ZX_OK);
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(ramdisk_vmo), kBlockSize);
  ASSERT_TRUE(ramdisk_or.is_ok()) << ramdisk_or.status_string();
  auto ramdisk = std::move(ramdisk_or.value());

  int fvm_dev_fd = ramdisk_get_block_fd(ramdisk.client());
  auto fvm_bind_result = BindFvm(fvm_dev_fd);
  ASSERT_TRUE(fvm_bind_result.is_ok()) << fvm_bind_result.status_string();

  const auto& blobfs_partition = *fvm_descriptor.partitions().begin();
  std::array<char, PATH_MAX> partition_path = {};
  fbl::unique_fd blobfs_fd(open_partition(nullptr, blobfs_partition.volume().type.data(),
                                          zx::sec(10).get(), partition_path.data()));
  ASSERT_TRUE(blobfs_fd.is_valid());

  auto fsck_options = default_fsck_options;
  fsck_options.always_modify = false;
  fsck_options.never_modify = true;
  fsck_options.verbose = true;
  fsck_options.force = true;
  ASSERT_EQ(fsck(partition_path.data(), DISK_FORMAT_MINFS, &fsck_options, &launch_stdio_sync),
            ZX_OK);
}

// The test will write the fvm image into a vmo, and then bring up a fvm driver,
// on top a ramdisk with the written data. The blobfs partition in the fvm driver,
// should pass FSCK if everything is correct.
TEST(AdapterTest, BlobfsAndMinfsPartitonInFvmImageFromFvmDescriptorPassesFsck) {
  constexpr uint64_t kImageSize = 500u * (1u << 20);
  auto fvm_options = MakeFvmOptions(kSliceSize);
  // 500 MB fvm image.
  fvm_options.target_volume_size = kImageSize;
  PartitionOptions partition_options;

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto minfs_partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(minfs_partition_or.is_ok()) << minfs_partition_or.error();
  auto minfs_partition = minfs_partition_or.take_value();

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto blobfs_partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(blobfs_partition_or.is_ok()) << blobfs_partition_or.error();
  auto blobfs_partition = blobfs_partition_or.take_value();

  auto fvm_descriptor_or = FvmDescriptor::Builder()
                               .SetOptions(fvm_options)
                               .AddPartition(std::move(minfs_partition))
                               .AddPartition(std::move(blobfs_partition))
                               .Build();
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  zx::vmo fvm_vmo;
  ASSERT_EQ(zx::vmo::create(fvm_options.target_volume_size.value(), 0u, &fvm_vmo), ZX_OK);
  VmoWriter fvm_writer(fvm_vmo.borrow(), kImageSize);

  auto write_result = fvm_descriptor.WriteBlockImage(fvm_writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  // Extent the fvm vmo to next block boundary of the ramdisk.
  constexpr uint64_t kBlockSize = 512;
  uint64_t block_count = GetBlockCount(0, fvm_writer.vmo_size(), kBlockSize);
  if (fvm_writer.vmo_size() % kBlockSize != 0) {
    ASSERT_EQ(fvm_vmo.set_size(kBlockSize * block_count), ZX_OK);
  }

  // Create ramdisk thingy.
  // duplicate fvm vmo.
  zx::vmo ramdisk_vmo;
  ASSERT_EQ(fvm_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &ramdisk_vmo), ZX_OK);
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(ramdisk_vmo), kBlockSize);
  ASSERT_TRUE(ramdisk_or.is_ok()) << ramdisk_or.status_string();
  auto ramdisk = std::move(ramdisk_or.value());

  int fvm_dev_fd = ramdisk_get_block_fd(ramdisk.client());
  auto fvm_bind_result = BindFvm(fvm_dev_fd);
  ASSERT_TRUE(fvm_bind_result.is_ok()) << fvm_bind_result.status_string();

  for (const auto& partition : fvm_descriptor.partitions()) {
    std::array<char, PATH_MAX> partition_path = {};
    fbl::unique_fd partition_fd(open_partition(nullptr, partition.volume().type.data(),
                                               zx::sec(10).get(), partition_path.data()));
    ASSERT_TRUE(partition_fd.is_valid());

    auto fsck_options = default_fsck_options;
    fsck_options.always_modify = false;
    fsck_options.never_modify = true;
    fsck_options.verbose = true;
    fsck_options.force = true;
    ASSERT_EQ(fsck(partition_path.data(),
                   partition.volume().name == "blob" ? DISK_FORMAT_BLOBFS : DISK_FORMAT_MINFS,
                   &fsck_options, &launch_stdio_sync),
              ZX_OK);
  }
}

}  // namespace
}  // namespace storage::volume_image
