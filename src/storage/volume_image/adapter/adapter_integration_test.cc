// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fpromise/result.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>
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

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "fidl/fuchsia.device/cpp/markers.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/storage/fvm/format.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"
#include "src/storage/volume_image/adapter/blobfs_partition.h"
#include "src/storage/volume_image/adapter/empty_partition.h"
#include "src/storage/volume_image/adapter/minfs_partition.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
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

constexpr std::string_view kFvmSparseImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_fvm.sparse.blk";

// Implementation of a Writer backed by a VMO.
class VmoWriter final : public Writer {
 public:
  VmoWriter(zx::unowned_vmo vmo, uint64_t size) : vmo_(std::move(vmo)), vmo_size_(size) {}

  void PoisonRange(uint64_t offset, uint64_t length) {
    ASSERT_GT(length, 0u);
    ASSERT_LE(offset + length, vmo_size_);
    std::vector<uint8_t> data;
    data.resize(fvm::kBlockSize, 0xaf);
    ASSERT_TRUE(Write(offset, data).is_ok());
  }

  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    if (offset + buffer.size() > vmo_size_) {
      auto result = zx::make_result(vmo_->set_size(offset + buffer.size()));
      if (result.is_error()) {
        return fpromise::error(std::string("VmoWriter::Write failed to extend vmo with status: ") +
                               result.status_string() + ".");
      }
      vmo_size_ = offset + buffer.size();
    }
    auto result = zx::make_result(vmo_->write(buffer.data(), offset, buffer.size()));
    if (result.is_error()) {
      return fpromise::error(std::string("VmoWriter::Write failed to write to vmo with status: ") +
                             result.status_string() + ".");
    }
    last_written_byte_ = std::max(last_written_byte_, offset + buffer.size());
    return fpromise::ok();
  }

  uint64_t vmo_size() const { return vmo_size_; }

  uint64_t last_written_byte() const { return last_written_byte_; }

 private:
  zx::unowned_vmo vmo_;
  uint64_t vmo_size_;
  uint64_t last_written_byte_ = 0;
};

// Implementation of a Writer backed by a VMO.
class VmoReader final : public Reader {
 public:
  VmoReader(zx::unowned_vmo vmo, uint64_t size) : vmo_(std::move(vmo)), vmo_size_(size) {}

  uint64_t length() const final { return vmo_size_; }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    auto result = zx::make_result(vmo_->read(buffer.data(), offset, buffer.size()));
    if (result.is_error()) {
      return fpromise::error(result.status_string());
    }
    return fpromise::ok();
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

constexpr uint64_t kSliceSize = UINT64_C(32) * (1u << 10);
constexpr uint64_t kImageSize = UINT64_C(500) * (1u << 20);
constexpr uint64_t kBlockSize = 512;

fpromise::result<Partition, std::string> GetBlobfsPartition(const PartitionOptions& options,
                                                            const FvmOptions& fvm_options) {
  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  if (blobfs_reader_or.is_error()) {
    return blobfs_reader_or.take_error_result();
  }
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  return CreateBlobfsFvmPartition(std::move(blobfs_reader), options, fvm_options);
}

fpromise::result<Partition, std::string> GetMinfsPartition(const PartitionOptions& options,
                                                           const FvmOptions& fvm_options) {
  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  if (minfs_reader_or.is_error()) {
    return minfs_reader_or.take_error_result();
  }
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  return CreateMinfsFvmPartition(std::move(minfs_reader), options, fvm_options);
}

struct WriteResult {
  zx::vmo image;
  VmoWriter image_writer;
};

fpromise::result<WriteResult, std::string> WriteFvmImage(const FvmDescriptor& fvm_descriptor) {
  const auto& fvm_options = fvm_descriptor.options();
  zx::vmo fvm_vmo;
  if (auto result = zx::vmo::create(kImageSize, 0u, &fvm_vmo); result != ZX_OK) {
    return fpromise::error("Failed to create fvm image vmo. Error Code: " + std::to_string(result) +
                           ".");
  }

  VmoWriter fvm_writer(fvm_vmo.borrow(), kImageSize);
  fvm_writer.PoisonRange(0, fvm_descriptor.metadata_required_size() * 2);
  if (testing::Test::HasFailure()) {
    return fpromise::error("Failed to poison fvm image vmo.");
  }

  auto header = internal::MakeHeader(fvm_options, 200);
  for (uint64_t i = 1; i <= fvm_descriptor.slice_count(); ++i) {
    fvm_writer.PoisonRange(header.GetSliceDataOffset(i), kSliceSize);
    if (testing::Test::HasFailure()) {
      return fpromise::error("Failed to poison fvm image vmo.");
    }
  }

  if (auto write_result = fvm_descriptor.WriteBlockImage(fvm_writer); write_result.is_error()) {
    return write_result.take_error_result();
  }

  // Extend the fvm vmo to next block boundary of the ramdisk.
  uint64_t block_count = GetBlockCount(0, fvm_writer.vmo_size(), kBlockSize);
  if (fvm_writer.vmo_size() % kBlockSize != 0) {
    if (auto result = fvm_vmo.set_size(kBlockSize * block_count); result != ZX_OK) {
      return fpromise::error("Failed to extend fvm image vmo to block boundary. Error Code: " +
                             std::to_string(result) + ".");
    }
  }

  return fpromise::ok(WriteResult{std::move(fvm_vmo), std::move(fvm_writer)});
}

fpromise::result<storage::RamDisk, std::string> LaunchFvm(zx::vmo& fvm_vmo) {
  zx::vmo ramdisk_vmo;
  if (auto result = fvm_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &ramdisk_vmo); result != ZX_OK) {
    return fpromise::error("Failed to extend fvm image vmo to block boundary. Error Code: " +
                           std::to_string(result) + ".");
  }
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(ramdisk_vmo), kBlockSize);
  if (ramdisk_or.is_error()) {
    return fpromise::error("Failed to create ramdisk for FVM. Error: " +
                           std::string(ramdisk_or.status_string()) + ".");
  }
  auto ramdisk = std::move(ramdisk_or.value());

  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  fidl::UnownedClientEnd<fuchsia_device::Controller> device(
      ramdisk_get_block_interface(ramdisk.client()));
  auto fvm_bind_result = BindFvm(device);
  if (fvm_bind_result.is_error()) {
    return fpromise::error("Failed to bind FVM to ramdisk. Error: " +
                           std::string(fvm_bind_result.status_string()) + ".");
  }

  return fpromise::ok(std::move(ramdisk));
}

void CheckPartitionsInRamdisk(const FvmDescriptor& fvm_descriptor) {
  for (const auto& partition : fvm_descriptor.partitions()) {
    std::string partition_path;
    fs_management::PartitionMatcher matcher{
        .type_guids = {uuid::Uuid(partition.volume().type.data())},
    };
    auto partition_fd_or =
        fs_management::OpenPartition(matcher, zx::sec(10).get(), &partition_path);
    ASSERT_EQ(partition_fd_or.status_value(), ZX_OK);

    if (partition.volume().name == "my-empty-partition") {
      // Check that allocated slices are equal to the slice count for max bytes.
      fdio_cpp::FdioCaller caller(std::move(partition_fd_or.value()));
      zx::result channel = caller.take_as<fuchsia_hardware_block::Block>();
      ASSERT_TRUE(channel.is_ok()) << channel.status_string();
      std::unique_ptr<block_client::RemoteBlockDevice> block_device;
      ASSERT_EQ(block_client::RemoteBlockDevice::Create(std::move(channel.value()), &block_device),
                ZX_OK);
      std::array<uint64_t, 2> slice_start = {0, 2};
      using VsliceRange = fuchsia_hardware_block_volume::wire::VsliceRange;
      std::array<VsliceRange, fuchsia_hardware_block_volume::wire::kMaxSliceRequests>

          ranges = {};
      uint64_t range_count;

      ASSERT_EQ(block_device->VolumeQuerySlices(slice_start.data(), slice_start.size(),
                                                reinterpret_cast<VsliceRange*>(ranges.data()),
                                                &range_count),
                ZX_OK);
      ASSERT_EQ(range_count, 2u);
      EXPECT_TRUE(ranges[0].allocated);
      EXPECT_EQ(ranges[0].count, 2u);
      EXPECT_FALSE(ranges[1].allocated);
      EXPECT_EQ(ranges[1].count, fvm::kMaxVSlices - 2);
      continue;
    }

    if (partition.volume().name == "internal") {
      // Check that allocated slices are equal to the slice count for max bytes.
      fdio_cpp::FdioCaller caller(std::move(partition_fd_or.value()));
      zx::result channel = caller.take_as<fuchsia_hardware_block::Block>();
      ASSERT_TRUE(channel.is_ok()) << channel.status_string();
      std::unique_ptr<block_client::RemoteBlockDevice> block_device;
      ASSERT_EQ(block_client::RemoteBlockDevice::Create(std::move(channel.value()), &block_device),
                ZX_OK);
      std::array<uint64_t, 2> slice_start = {0, 4};
      using VsliceRange = fuchsia_hardware_block_volume::wire::VsliceRange;
      std::array<VsliceRange, fuchsia_hardware_block_volume::wire::kMaxSliceRequests>

          ranges = {};
      uint64_t range_count;

      ASSERT_EQ(block_device->VolumeQuerySlices(slice_start.data(), slice_start.size(),
                                                reinterpret_cast<VsliceRange*>(ranges.data()),
                                                &range_count),
                ZX_OK);
      ASSERT_EQ(range_count, 2u);
      EXPECT_TRUE(ranges[0].allocated);
      EXPECT_EQ(ranges[0].count, 4u);
      EXPECT_FALSE(ranges[1].allocated);
      EXPECT_EQ(ranges[1].count, fvm::kMaxVSlices - 4);
      continue;
    }

    fs_management::FsckOptions fsck_options{
        .verbose = true,
        .never_modify = true,
        .always_modify = false,
        .force = true,
    };
    if (partition.volume().name == "blobfs") {
      fsck_options.component_child_name = "test-blobfs";
      fsck_options.component_collection_name = "fs-collection";
    }
    EXPECT_EQ(
        fs_management::Fsck(partition_path,
                            partition.volume().name == "blobfs" ? fs_management::kDiskFormatBlobfs
                                                                : fs_management::kDiskFormatMinfs,
                            fsck_options, &fs_management::LaunchStdioSync),
        ZX_OK);
  }
}

// The test will write the fvm image into a vmo, and then bring up a fvm driver,
// on top a ramdisk with the written data. The blobfs partition in the fvm driver,
// should pass FSCK if everything is correct.
TEST(AdapterTest, BlobfsPartitonInFvmImagePassesFsck) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  // 500 MB fvm image.
  fvm_options.target_volume_size = kImageSize;
  PartitionOptions partition_options;

  auto partition_or = GetBlobfsPartition(partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto fvm_descriptor_or =
      FvmDescriptor::Builder().SetOptions(fvm_options).AddPartition(std::move(partition)).Build();
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  auto write_result = WriteFvmImage(fvm_descriptor);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  auto [fvm_vmo, fvm_writer] = write_result.take_value();

  auto ramdisk_handle = LaunchFvm(fvm_vmo);

  ASSERT_NO_FATAL_FAILURE(CheckPartitionsInRamdisk(fvm_descriptor));
}

// The test will write the fvm image into a vmo, and then bring up a fvm driver,
// on top a ramdisk with the written data. The blobfs partition in the fvm driver,
// should pass FSCK if everything is correct.
TEST(AdapterTest, MinfsPartitonInFvmImagePassesFsck) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  // 500 MB fvm image.
  fvm_options.target_volume_size = kImageSize;
  PartitionOptions partition_options;

  auto partition_or = GetMinfsPartition(partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto fvm_descriptor_or =
      FvmDescriptor::Builder().SetOptions(fvm_options).AddPartition(std::move(partition)).Build();
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  auto write_result = WriteFvmImage(fvm_descriptor);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  auto [fvm_vmo, fvm_writer] = write_result.take_value();

  auto ramdisk_handle = LaunchFvm(fvm_vmo);

  ASSERT_NO_FATAL_FAILURE(CheckPartitionsInRamdisk(fvm_descriptor));
}

// The test will write the fvm image into a vmo, and then bring up a fvm driver,
// on top a ramdisk with the written data. The blobfs partition in the fvm driver,
// should pass FSCK if everything is correct.
TEST(AdapterTest, BlobfsMinfsAndEmptyPartitionInFvmImagePassesFsck) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  fvm_options.target_volume_size = kImageSize;
  PartitionOptions partition_options;

  auto minfs_partition_or = GetMinfsPartition(partition_options, fvm_options);
  ASSERT_TRUE(minfs_partition_or.is_ok()) << minfs_partition_or.error();
  auto minfs_partition = minfs_partition_or.take_value();

  auto blobfs_partition_or = GetBlobfsPartition(partition_options, fvm_options);
  ASSERT_TRUE(blobfs_partition_or.is_ok()) << blobfs_partition_or.error();
  auto blobfs_partition = blobfs_partition_or.take_value();

  auto empty_partition_options = partition_options;
  empty_partition_options.max_bytes = fvm_options.slice_size + 1;
  auto empty_partition_or = CreateEmptyFvmPartition(empty_partition_options, fvm_options);
  ASSERT_TRUE(empty_partition_or.is_ok()) << empty_partition_or.error();
  auto empty_partition = empty_partition_or.take_value();
  empty_partition.volume().name = "my-empty-partition";
  // Just some fixed number, since 00000, is taken by the ramdisk.
  empty_partition.volume().type[0] = 1;
  empty_partition.volume().type[1] = 1;
  empty_partition.volume().type[2] = 1;

  auto fvm_descriptor_or = FvmDescriptor::Builder()
                               .SetOptions(fvm_options)
                               .AddPartition(std::move(minfs_partition))
                               .AddPartition(std::move(blobfs_partition))
                               .AddPartition(std::move(empty_partition))
                               .Build();
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  auto write_result = WriteFvmImage(fvm_descriptor);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  auto [fvm_vmo, fvm_writer] = write_result.take_value();

  auto ramdisk_handle = LaunchFvm(fvm_vmo);

  ASSERT_NO_FATAL_FAILURE(CheckPartitionsInRamdisk(fvm_descriptor));
}

// The test will write the fvm image into a vmo, and then bring up a fvm driver,
// on top a ramdisk with the written data. The blobfs partition in the fvm driver,
// should pass FSCK if everything is correct.
TEST(AdapterTest, CompressedSparseImageToFvmImagePassesFsck) {
  auto compressed_sparse_reader_or = FdReader::Create(kFvmSparseImagePath);
  ASSERT_TRUE(compressed_sparse_reader_or.is_ok()) << compressed_sparse_reader_or.error();
  FdReader compressed_sparse_reader = compressed_sparse_reader_or.take_value();

  // Decompress the image.
  zx::vmo decompressed_sparse_image;
  ASSERT_EQ(zx::vmo::create(kImageSize, 0, &decompressed_sparse_image), ZX_OK);
  auto decompressed_writer = VmoWriter(decompressed_sparse_image.borrow(), kImageSize);

  auto decompress_result =
      FvmSparseDecompressImage(0, compressed_sparse_reader, decompressed_writer);
  ASSERT_TRUE(decompress_result.is_ok()) << decompress_result.error();
  ASSERT_TRUE(decompress_result.value());

  // Read the decompressed image.
  auto fvm_descriptor_or =
      FvmSparseReadImage(0, std::make_unique<VmoReader>(decompressed_sparse_image.borrow(),
                                                        decompressed_writer.last_written_byte()));
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  auto write_result = WriteFvmImage(fvm_descriptor);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  auto [fvm_vmo, fvm_writer] = write_result.take_value();

  auto ramdisk_handle = LaunchFvm(fvm_vmo);
  ASSERT_NO_FATAL_FAILURE(CheckPartitionsInRamdisk(fvm_descriptor));
}

TEST(AdapterTest, CompressedSparseImageWithoutExplicitDecompressionToFvmImagePassesFsck) {
  auto compressed_sparse_reader_or = FdReader::Create(kFvmSparseImagePath);
  ASSERT_TRUE(compressed_sparse_reader_or.is_ok()) << compressed_sparse_reader_or.error();
  FdReader compressed_sparse_reader = compressed_sparse_reader_or.take_value();

  // Read the decompressed image.
  auto fvm_descriptor_or =
      FvmSparseReadImage(0, std::make_unique<FdReader>(std::move(compressed_sparse_reader)));
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  auto write_result = WriteFvmImage(fvm_descriptor);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  auto [fvm_vmo, fvm_writer] = write_result.take_value();

  auto ramdisk_handle = LaunchFvm(fvm_vmo);
  ASSERT_NO_FATAL_FAILURE(CheckPartitionsInRamdisk(fvm_descriptor));
}

TEST(AdapterTest, CheckWithMaxVolumeSizeSet) {
  auto compressed_sparse_reader_or = FdReader::Create(kFvmSparseImagePath);
  ASSERT_TRUE(compressed_sparse_reader_or.is_ok()) << compressed_sparse_reader_or.error();
  FdReader compressed_sparse_reader = compressed_sparse_reader_or.take_value();

  // Read the decompressed image.
  auto fvm_descriptor_or =
      FvmSparseReadImage(0, std::make_unique<FdReader>(std::move(compressed_sparse_reader)));
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor_base = fvm_descriptor_or.take_value();

  FvmOptions options = fvm_descriptor_base.options();
  options.target_volume_size = kImageSize;
  options.max_volume_size = 2 * kImageSize;
  options.compression.schema = CompressionSchema::kNone;

  fvm_descriptor_or =
      FvmDescriptor::Builder(std::move(fvm_descriptor_base)).SetOptions(options).Build();
  ASSERT_TRUE(fvm_descriptor_or.is_ok()) << fvm_descriptor_or.error();
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  auto write_result = WriteFvmImage(fvm_descriptor);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  auto [fvm_vmo, fvm_writer] = write_result.take_value();

  auto ramdisk_handle = LaunchFvm(fvm_vmo);
  ASSERT_NO_FATAL_FAILURE(CheckPartitionsInRamdisk(fvm_descriptor));
}

}  // namespace
}  // namespace storage::volume_image
