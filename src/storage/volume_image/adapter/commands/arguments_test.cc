// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/array.h>

#include <array>
#include <cstdint>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/format.h"
#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/guid.h"

namespace storage::volume_image {
namespace {

constexpr uint64_t kKilo = 1u << 10;
constexpr uint64_t kMega = kKilo * kKilo;
constexpr uint64_t kGiga = kKilo * kMega;

TEST(ArgumentTest, CommandFromStringIsOk) {
  EXPECT_EQ(CommandFromString("create"), Command::kCreate);
  EXPECT_EQ(CommandFromString("sparse"), Command::kCreateSparse);
  EXPECT_EQ(CommandFromString("notacommand"), Command::kUnsupported);
}

TEST(ArgumentTest, PartitionParamsFromArgsIsok) {
  std::array<std::string_view, 44> kArgs = {
      "--blob",
      "path",
      "--minimum-inodes",
      "123",
      "--minimum-data-bytes",
      "1M",
      "--maximum-bytes",
      "12G",
      "--data",
      "path2",
      "--minimum-inodes",
      "12",
      "--minimum-data-bytes",
      "1K",
      "--maximum-bytes",
      "11M",
      "--with-empty-minfs",
      "--with-empty-account-partition",
      "--data-unsafe",
      "path3",
      "--minimum-inodes",
      "13",
      "--minimum-data-bytes",
      "10K",
      "--maximum-bytes",
      "1313",
      "--system",
      "path4",
      "--minimum-inodes",
      "14",
      "--minimum-data-bytes",
      "11K",
      "--maximum-bytes",
      "1",
      "--default",
      "path5",
      "--minimum-inodes",
      "1K",
      "--minimum-data-bytes",
      "11K",
      "--maximum-bytes",
      "131313",
      "--reserve-slices",
      "5",
  };
  FvmOptions options;
  options.slice_size = 8192;

  auto params_or = PartitionParams::FromArguments(kArgs, options);
  ASSERT_TRUE(params_or.is_ok()) << params_or.error();
  std::vector<PartitionParams> params = params_or.take_value();

  ASSERT_EQ(params.size(), 8u);

  auto blob_params = params[0];
  EXPECT_EQ(blob_params.label, "");
  EXPECT_EQ(blob_params.source_image_path, "path");
  EXPECT_EQ(blob_params.format, PartitionImageFormat::kBlobfs);
  EXPECT_FALSE(blob_params.type_guid.has_value());
  EXPECT_FALSE(blob_params.encrypted);
  EXPECT_EQ(blob_params.options.max_bytes.value(), 12 * kGiga);
  EXPECT_EQ(blob_params.options.min_data_bytes.value(), 1 * kMega);
  EXPECT_EQ(blob_params.options.min_inode_count.value(), 123u);

  auto data_params = params[1];
  EXPECT_EQ(data_params.label, "data");
  EXPECT_EQ(data_params.source_image_path, "path2");
  EXPECT_EQ(data_params.format, PartitionImageFormat::kMinfs);
  EXPECT_FALSE(data_params.type_guid.has_value());
  EXPECT_TRUE(data_params.encrypted);
  EXPECT_EQ(data_params.options.max_bytes.value(), 11 * kMega);
  EXPECT_EQ(data_params.options.min_data_bytes.value(), 1u * kKilo);
  EXPECT_EQ(data_params.options.min_inode_count.value(), 12u);

  auto data_unsafe_params = params[2];
  EXPECT_EQ(data_unsafe_params.label, "data-unsafe");
  EXPECT_EQ(data_unsafe_params.source_image_path, "path3");
  EXPECT_EQ(data_unsafe_params.format, PartitionImageFormat::kMinfs);
  EXPECT_FALSE(data_unsafe_params.type_guid.has_value());
  EXPECT_FALSE(data_unsafe_params.encrypted);
  EXPECT_EQ(data_unsafe_params.options.max_bytes.value(), 1313u);
  EXPECT_EQ(data_unsafe_params.options.min_data_bytes.value(), 10 * kKilo);
  EXPECT_EQ(data_unsafe_params.options.min_inode_count.value(), 13u);

  auto system_params = params[3];
  EXPECT_EQ(system_params.label, "system");
  EXPECT_EQ(system_params.source_image_path, "path4");
  EXPECT_EQ(system_params.format, PartitionImageFormat::kMinfs);
  EXPECT_FALSE(system_params.type_guid.has_value());
  EXPECT_FALSE(system_params.encrypted);
  EXPECT_EQ(system_params.options.max_bytes.value(), 1u);
  EXPECT_EQ(system_params.options.min_data_bytes.value(), 11u * kKilo);
  EXPECT_EQ(system_params.options.min_inode_count.value(), 14u);

  auto default_params = params[4];
  EXPECT_EQ(default_params.label, "default");
  EXPECT_EQ(default_params.source_image_path, "path5");
  EXPECT_EQ(default_params.format, PartitionImageFormat::kMinfs);
  EXPECT_FALSE(default_params.type_guid.has_value());
  EXPECT_FALSE(default_params.encrypted);
  EXPECT_EQ(default_params.options.max_bytes.value(), 131313u);
  EXPECT_EQ(default_params.options.min_data_bytes.value(), 11u * kKilo);
  EXPECT_EQ(default_params.options.min_inode_count.value(), 1 * kKilo);

  auto empty_data_params = params[5];
  EXPECT_EQ(empty_data_params.label, "data");
  EXPECT_EQ(empty_data_params.source_image_path, "");
  EXPECT_EQ(empty_data_params.format, PartitionImageFormat::kEmptyPartition);
  uint8_t kDataGuid[] = GUID_DATA_VALUE;
  EXPECT_TRUE(memcmp(empty_data_params.type_guid->data(), kDataGuid, kGuidLength) == 0);
  EXPECT_FALSE(empty_data_params.encrypted);
  EXPECT_EQ(empty_data_params.options.max_bytes.value(), options.slice_size + 1);

  auto empty_account_params = params[6];
  EXPECT_EQ(empty_account_params.label, "account");
  EXPECT_EQ(empty_account_params.source_image_path, "");
  EXPECT_EQ(empty_account_params.format, PartitionImageFormat::kEmptyPartition);
  EXPECT_TRUE(memcmp(empty_account_params.type_guid->data(), kDataGuid, kGuidLength) == 0);
  EXPECT_FALSE(empty_account_params.encrypted);
  EXPECT_EQ(empty_account_params.options.max_bytes.value(), options.slice_size + 1);

  auto reserved_partition_params = params[7];
  EXPECT_EQ(reserved_partition_params.label, "internal");
  EXPECT_EQ(reserved_partition_params.source_image_path, "");
  EXPECT_EQ(reserved_partition_params.format, PartitionImageFormat::kEmptyPartition);
  EXPECT_TRUE(memcmp(reserved_partition_params.type_guid->data(),
                     fvm::kReservedPartitionTypeGuid.data(), kGuidLength) == 0);
  EXPECT_FALSE(reserved_partition_params.encrypted);
  EXPECT_EQ(reserved_partition_params.options.max_bytes.value(), 5 * options.slice_size);
}

TEST(ArgumentTest, CreateParamsFromArgsIsOk) {
  std::array<std::string_view, 21> kArgs = {
      "binary",      "output_path",      "create", "--blob",
      "blobfs_path", "--minimum-inodes", "123",    "--minimum-data-bytes",
      "1M",          "--maximum-bytes",  "12G",    "--slice",
      "8K",          "--offset",         "1234",   "--length",
      "1234567",     "--max-disk-size",  "160M",   "--compress",
      "lz4",
  };

  {
    auto params_or = CreateParams::FromArguments(fbl::Span<std::string_view>(kArgs).subspan(0, 19));
    auto params = params_or.take_value();
    EXPECT_EQ(params.fvm_options.compression.schema, CompressionSchema::kNone);
  }

  {
    auto params_or = CreateParams::FromArguments(kArgs);
    ASSERT_TRUE(params_or.is_ok()) << params_or.error();
    auto params = params_or.take_value();

    EXPECT_EQ(params.format, FvmImageFormat::kBlockImage);
    EXPECT_EQ(params.output_path, "output_path");
    EXPECT_EQ(params.offset, 1234u);
    EXPECT_EQ(params.length, 1234567u);
    EXPECT_EQ(params.fvm_options.slice_size, 8 * kKilo);
    EXPECT_EQ(params.fvm_options.target_volume_size, 1234567u);
    EXPECT_EQ(params.fvm_options.max_volume_size, 160 * kMega);
    EXPECT_EQ(params.fvm_options.compression.schema, CompressionSchema::kLz4);
    EXPECT_TRUE(params.is_output_embedded);

    ASSERT_EQ(params.partitions.size(), 1u);

    auto blob_params = params.partitions[0];
    EXPECT_EQ(blob_params.label, "");
    EXPECT_EQ(blob_params.source_image_path, "blobfs_path");
    EXPECT_EQ(blob_params.format, PartitionImageFormat::kBlobfs);
    EXPECT_FALSE(blob_params.type_guid.has_value());
    EXPECT_FALSE(blob_params.encrypted);
    EXPECT_EQ(blob_params.options.max_bytes.value(), 12 * kGiga);
    EXPECT_EQ(blob_params.options.min_data_bytes.value(), 1 * kMega);
    EXPECT_EQ(blob_params.options.min_inode_count.value(), 123u);
  }

  {
    kArgs[2] = "sparse";
    auto params_or = CreateParams::FromArguments(kArgs);
    ASSERT_TRUE(params_or.is_ok()) << params_or.error();
    auto params = params_or.take_value();

    EXPECT_EQ(params.format, FvmImageFormat::kSparseImage);
    EXPECT_EQ(params.output_path, "output_path");
    EXPECT_EQ(params.offset, 1234u);
    EXPECT_EQ(params.length, 1234567u);
    EXPECT_EQ(params.fvm_options.slice_size, 8 * kKilo);
    EXPECT_EQ(params.fvm_options.target_volume_size, 1234567u);
    EXPECT_EQ(params.fvm_options.max_volume_size, 160 * kMega);
    EXPECT_EQ(params.fvm_options.compression.schema, CompressionSchema::kLz4);
    EXPECT_TRUE(params.is_output_embedded);

    ASSERT_EQ(params.partitions.size(), 1u);

    auto blob_params = params.partitions[0];
    EXPECT_EQ(blob_params.label, "");
    EXPECT_EQ(blob_params.source_image_path, "blobfs_path");
    EXPECT_EQ(blob_params.format, PartitionImageFormat::kBlobfs);
    EXPECT_FALSE(blob_params.type_guid.has_value());
    EXPECT_FALSE(blob_params.encrypted);
    EXPECT_EQ(blob_params.options.max_bytes.value(), 12 * kGiga);
    EXPECT_EQ(blob_params.options.min_data_bytes.value(), 1 * kMega);
    EXPECT_EQ(blob_params.options.min_inode_count.value(), 123u);
  }

  {
    auto params_or = CreateParams::FromArguments(fbl::Span<std::string_view>(kArgs).subspan(0, 19));
    ASSERT_TRUE(params_or.is_ok()) << params_or.error();
    auto params = params_or.take_value();
    EXPECT_EQ(params.fvm_options.compression.schema, CompressionSchema::kNone);
  }
}

TEST(ArgumentTest, CreateParamsFromArgsWithoutOutputPathOrCommandIsError) {
  std::array<std::string_view, 2> kArgsWithoutCommand = {
      "binary",
      "output_path",
  };

  ASSERT_TRUE(CreateParams::FromArguments(kArgsWithoutCommand).is_error());

  std::array<std::string_view, 2> kArgsWithoutOutputPath = {
      "binary",
      "create",
  };

  ASSERT_TRUE(CreateParams::FromArguments(kArgsWithoutOutputPath).is_error());

  std::array<std::string_view, 3> kArgsWithWrongCommand = {
      "binary",
      "output_path",
      "notcreate",
  };

  ASSERT_TRUE(CreateParams::FromArguments(kArgsWithWrongCommand).is_error());
}

TEST(ArgumentTest, ArgumentWithMissingValueIsError) {
  std::vector<std::string_view> args = {"--blob"};
  FvmOptions options;
  options.slice_size = 8192;
  ASSERT_TRUE(PartitionParams::FromArguments(args, options).is_error());

  args.push_back("path");
  args.push_back("--minimum-data-bytes");
  ASSERT_TRUE(PartitionParams::FromArguments(args, options).is_error());
}

TEST(ArgumentTest, ArgumentWithWrongTypeIsError) {
  std::array<std::string_view, 4> kArgs = {"--blob", "123", "--minimum-data-bytes", "ggwp"};
  FvmOptions options;
  options.slice_size = 8192;
  ASSERT_TRUE(PartitionParams::FromArguments(kArgs, options).is_error());
}

TEST(ArgumentTest, CreateParamsFromSparseRegressionCheck) {
  std::array<std::string_view, 17> kArgs = {
      "fvm",           "test_fvm.sparse.blk",
      "sparse",        "--compress",
      "lz4",           "--slice",
      "8388608",       "--blob",
      "test_blob.blk", "--data",
      "test_data.blk", "--minimum-inodes",
      "600000",        "--minimum-data-bytes",
      "10M",           "--maximum-bytes",
      "218103808",
  };

  auto create_params_or = CreateParams::FromArguments(kArgs);
  ASSERT_TRUE(create_params_or.is_ok()) << create_params_or.error();
  auto create_params = create_params_or.take_value();

  EXPECT_EQ(create_params.output_path, kArgs[1]);
  EXPECT_EQ(create_params.format, FvmImageFormat::kSparseImage);
  EXPECT_EQ(create_params.trim_image, false);
  EXPECT_EQ(create_params.is_output_embedded, false);
  EXPECT_FALSE(create_params.offset.has_value());
  EXPECT_EQ(create_params.fvm_options.slice_size, 8 * kMega);
  EXPECT_EQ(create_params.fvm_options.compression.schema, CompressionSchema::kLz4);
  EXPECT_FALSE(create_params.fvm_options.target_volume_size.has_value());
  EXPECT_FALSE(create_params.fvm_options.max_volume_size.has_value());

  ASSERT_EQ(create_params.partitions.size(), 2u);

  auto blobfs_partition = create_params.partitions[0];
  EXPECT_EQ(blobfs_partition.source_image_path, "test_blob.blk");
  EXPECT_EQ(blobfs_partition.label, "");
  EXPECT_EQ(blobfs_partition.encrypted, false);
  EXPECT_EQ(blobfs_partition.format, PartitionImageFormat::kBlobfs);
  EXPECT_FALSE(blobfs_partition.type_guid.has_value());

  EXPECT_FALSE(blobfs_partition.options.max_bytes.has_value());
  EXPECT_FALSE(blobfs_partition.options.min_data_bytes.has_value());
  EXPECT_FALSE(blobfs_partition.options.min_inode_count.has_value());

  auto minfs_partition = create_params.partitions[1];
  EXPECT_EQ(minfs_partition.source_image_path, "test_data.blk");
  EXPECT_EQ(minfs_partition.label, "data");
  EXPECT_EQ(minfs_partition.encrypted, true);
  EXPECT_EQ(minfs_partition.format, PartitionImageFormat::kMinfs);
  EXPECT_FALSE(minfs_partition.type_guid.has_value());

  ASSERT_TRUE(minfs_partition.options.max_bytes.has_value());
  ASSERT_TRUE(minfs_partition.options.min_data_bytes.has_value());
  ASSERT_TRUE(minfs_partition.options.min_inode_count.has_value());

  EXPECT_EQ(minfs_partition.options.max_bytes.value(), 218103808u);
  EXPECT_EQ(minfs_partition.options.min_data_bytes.value(), 10 * kMega);
  EXPECT_EQ(minfs_partition.options.min_inode_count.value(), 600000u);
}

TEST(ArgumentTest, PaveParamsFromArgsIsOk) {
  std::array<std::string_view, 9> kArgs = {
      "fvm", "test_fvm.blk",    "pave", "--sparse", "my_sparse_image", "--length",
      "10M", "--max-disk-size", "25G",
  };

  auto pave_params_or = PaveParams::FromArguments(kArgs);
  ASSERT_TRUE(pave_params_or.is_ok()) << pave_params_or.error();
  auto pave_params = pave_params_or.take_value();

  EXPECT_EQ(pave_params.output_path, kArgs[1]);
  EXPECT_EQ(pave_params.input_path, kArgs[4]);
  EXPECT_EQ(pave_params.length.value(), 10 * kMega);
  EXPECT_EQ(pave_params.fvm_options.target_volume_size.value(), 10 * kMega);
  EXPECT_EQ(pave_params.fvm_options.max_volume_size.value(), 25 * kGiga);
  EXPECT_EQ(pave_params.type, TargetType::kFile);
  EXPECT_FALSE(pave_params.is_output_embedded);
  EXPECT_FALSE(pave_params.offset.has_value());
  EXPECT_FALSE(pave_params.max_bad_blocks.has_value());
}

TEST(ArgumentTest, PaveParamsFromArgsWithDiskTypeIsOk) {
  std::array<std::string_view, 13> kArgs = {
      "fvm",
      "test_fvm.blk",
      "pave",
      "--sparse",
      "my_sparse_image",
      "--length",
      "10M",
      "--max-disk-size",
      "25G",
      "--max-bad-blocks",
      "25",
      "--disk-type",
      "mtd",
  };
  {
    auto pave_params_or = PaveParams::FromArguments(kArgs);
    ASSERT_TRUE(pave_params_or.is_ok()) << pave_params_or.error();
    auto pave_params = pave_params_or.take_value();

    EXPECT_EQ(pave_params.output_path, kArgs[1]);
    EXPECT_EQ(pave_params.input_path, kArgs[4]);
    EXPECT_EQ(pave_params.length.value(), 10 * kMega);
    EXPECT_EQ(pave_params.fvm_options.target_volume_size.value(), 10 * kMega);
    EXPECT_EQ(pave_params.fvm_options.max_volume_size.value(), 25 * kGiga);
    EXPECT_EQ(pave_params.type, TargetType::kMtd);
    EXPECT_EQ(pave_params.max_bad_blocks.value(), 25u);
    EXPECT_FALSE(pave_params.is_output_embedded);
    EXPECT_FALSE(pave_params.offset.has_value());
  }

  {
    kArgs.back() = "file";
    auto pave_params_or = PaveParams::FromArguments(kArgs);
    ASSERT_TRUE(pave_params_or.is_ok()) << pave_params_or.error();
    auto pave_params = pave_params_or.take_value();

    EXPECT_EQ(pave_params.output_path, kArgs[1]);
    EXPECT_EQ(pave_params.input_path, kArgs[4]);
    EXPECT_EQ(pave_params.length.value(), 10 * kMega);
    EXPECT_EQ(pave_params.fvm_options.target_volume_size.value(), 10 * kMega);
    EXPECT_EQ(pave_params.fvm_options.max_volume_size.value(), 25 * kGiga);
    EXPECT_EQ(pave_params.max_bad_blocks.value(), 25u);
    EXPECT_EQ(pave_params.type, TargetType::kFile);
    EXPECT_FALSE(pave_params.is_output_embedded);
    EXPECT_FALSE(pave_params.offset.has_value());
  }

  {
    kArgs.back() = "block_device";
    auto pave_params_or = PaveParams::FromArguments(kArgs);
    ASSERT_TRUE(pave_params_or.is_ok()) << pave_params_or.error();
    auto pave_params = pave_params_or.take_value();

    EXPECT_EQ(pave_params.output_path, kArgs[1]);
    EXPECT_EQ(pave_params.input_path, kArgs[4]);
    EXPECT_EQ(pave_params.length.value(), 10 * kMega);
    EXPECT_EQ(pave_params.fvm_options.target_volume_size.value(), 10 * kMega);
    EXPECT_EQ(pave_params.fvm_options.max_volume_size.value(), 25 * kGiga);
    EXPECT_EQ(pave_params.type, TargetType::kBlockDevice);
    EXPECT_EQ(pave_params.max_bad_blocks.value(), 25u);
    EXPECT_FALSE(pave_params.is_output_embedded);
    EXPECT_FALSE(pave_params.offset.has_value());
  }
}

TEST(ArgumentTest, ExtendParamsFromArgsIsOk) {
  auto kArgs = cpp20::to_array<std::string_view>({
      "fvm",
      "test_fvm.sparse.blk",
      "extend",
      "--length",
      "10M",
      "--resize-image-file-to-fit",
      "--length-is-lowerbound",
  });

  {
    auto params_or = ExtendParams::FromArguments(kArgs);
    ASSERT_TRUE(params_or.is_ok()) << params_or.error();
    auto params = params_or.take_value();

    EXPECT_EQ(params.image_path, kArgs[1]);
    EXPECT_EQ(params.length.value(), 10 * kMega);
    EXPECT_TRUE(params.should_trim);
    EXPECT_TRUE(params.should_use_max_partition_size);
  }

  {
    auto args_without_trim = fbl::Span<std::string_view>(kArgs).subspan(0, kArgs.size() - 1);
    auto params_or = ExtendParams::FromArguments(args_without_trim);
    ASSERT_TRUE(params_or.is_ok()) << params_or.error();
    auto params = params_or.take_value();

    EXPECT_EQ(params.image_path, kArgs[1]);
    EXPECT_EQ(params.length.value(), 10 * kMega);
    EXPECT_TRUE(params.should_trim);
    EXPECT_FALSE(params.should_use_max_partition_size);
  }

  {
    auto args_without_trim_or_fit = fbl::Span<std::string_view>(kArgs).subspan(0, kArgs.size() - 2);
    auto params_or = ExtendParams::FromArguments(args_without_trim_or_fit);
    ASSERT_TRUE(params_or.is_ok()) << params_or.error();
    auto params = params_or.take_value();

    EXPECT_EQ(params.image_path, kArgs[1]);
    EXPECT_EQ(params.length.value(), 10 * kMega);
    EXPECT_FALSE(params.should_trim);
    EXPECT_FALSE(params.should_use_max_partition_size);
  }
}

TEST(ArgumentTest, SizeParamsFromArgsIsOk) {
  auto kArgs = cpp20::to_array<std::string_view>({
      "fvm",
      "test_fvm.sparse.blk",
      "size",
      "--disk",
      "10M",
  });

  {
    auto params_or = SizeParams::FromArguments(kArgs);
    ASSERT_TRUE(params_or.is_ok()) << params_or.error();
    auto params = params_or.take_value();

    EXPECT_EQ(params.image_path, kArgs[1]);
    EXPECT_EQ(params.length.value(), 10 * kMega);
  }

  {
    auto args_without_disk = fbl::Span<std::string_view>(kArgs).subspan(0, kArgs.size() - 2);
    auto params_or = SizeParams::FromArguments(args_without_disk);
    ASSERT_TRUE(params_or.is_ok()) << params_or.error();
    auto params = params_or.take_value();

    EXPECT_EQ(params.image_path, kArgs[1]);
    EXPECT_FALSE(params.length.has_value());
  }
}

}  // namespace
}  // namespace storage::volume_image
