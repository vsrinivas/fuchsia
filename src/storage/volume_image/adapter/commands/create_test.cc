// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_check.h"
#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/fd_test_helper.h"
#include "src/storage/volume_image/utils/fd_writer.h"
#include "src/storage/volume_image/utils/lz4_decompressor.h"

namespace storage::volume_image {
namespace {

constexpr std::string_view kBlobfsImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_blobfs.blk";

constexpr std::string_view kMinfsImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_minfs.blk";

constexpr uint64_t kImageSize = 350 * (1 << 20);
constexpr uint64_t kInitialImageSize = 150 * (1 << 20);
constexpr uint64_t kSliceSize = 32 * (1u << 10);

CreateParams MakeParams() {
  CreateParams params;
  params.output_path = "some_path";
  params.format = FvmImageFormat::kBlockImage;
  params.is_output_embedded = false;

  params.fvm_options.target_volume_size = kInitialImageSize;
  params.fvm_options.max_volume_size = kImageSize;
  params.fvm_options.slice_size = kSliceSize;
  params.fvm_options.compression.schema = CompressionSchema::kNone;

  PartitionParams minfs;
  minfs.encrypted = false;
  minfs.format = PartitionImageFormat::kMinfs;
  minfs.source_image_path = kMinfsImagePath;
  params.partitions.push_back(minfs);

  PartitionParams blobfs;
  blobfs.encrypted = false;
  blobfs.format = PartitionImageFormat::kBlobfs;
  blobfs.source_image_path = kBlobfsImagePath;
  params.partitions.push_back(blobfs);

  return params;
}

TEST(CreateCommandTest, NoOutputPathIsError) {
  CreateParams params = MakeParams();
  params.output_path = "";

  ASSERT_TRUE(Create(params).is_error());
}

TEST(CreateCommandTest, EmbeddedOutputWithoutOffsetIsError) {
  CreateParams params = MakeParams();
  params.is_output_embedded = true;
  params.offset = std::nullopt;
  params.length = kInitialImageSize;

  ASSERT_TRUE(Create(params).is_error());
}

TEST(CreateCommandTest, EmbeddedOutputWithoutLengthIsError) {
  CreateParams params = MakeParams();
  params.is_output_embedded = true;
  params.offset = 12345;
  params.length = std::nullopt;

  ASSERT_TRUE(Create(params).is_error());
}

TEST(CreateCommandTest, SliceSizeZeroIsError) {
  CreateParams params = MakeParams();
  params.fvm_options.slice_size = 0;

  ASSERT_TRUE(Create(params).is_error());
}

TEST(CreateCommandTest, SliceSizeNoMultipleOfFvmBlockIsError) {
  CreateParams params = MakeParams();
  params.fvm_options.slice_size = fvm::kBlockSize + 1;

  ASSERT_TRUE(Create(params).is_error());
}

TEST(CreateCommandTest, UnableToCreateOutputPathIsError) {
  CreateParams params = MakeParams();
  params.output_path = "/absolute/path/doesnt/exist/file.txt";
  ASSERT_TRUE(Create(params).is_error());
}

TEST(CreateCommandTest, PartitionOptionsArePropagated) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  // Set really small max bytes for a partiton, and it should fail if they are propagated.

  CreateParams params = MakeParams();
  params.output_path = output_file_or.value().path();
  params.format = FvmImageFormat::kBlockImage;
  params.fvm_options.compression.schema = CompressionSchema::kNone;

  // Max bytes.
  // We only test max bytes, because is the only one that has hard failure.
  params.partitions.front().options.max_bytes = 1;

  ASSERT_TRUE(Create(params).is_error());
}

TEST(CreateCommandTest, CreateFvmBlockImageIsOk) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  CreateParams params = MakeParams();
  params.output_path = output_file_or.value().path();
  params.format = FvmImageFormat::kBlockImage;
  params.fvm_options.compression.schema = CompressionSchema::kNone;

  ASSERT_TRUE(Create(params).is_ok());

  fbl::unique_fd fvm_fd(open(output_file_or.value().path().data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());

  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());
}

fpromise::result<void, std::string> Decompress(std::string_view path) {
  auto output_file_or = TempFile::Create();
  if (output_file_or.is_error()) {
    return output_file_or.take_error_result();
  }

  Lz4Decompressor decompressor;

  auto reader_or = FdReader::Create(path);
  if (reader_or.is_error()) {
    return reader_or.take_error_result();
  }
  auto reader = reader_or.take_value();

  auto writer_or = FdWriter::Create(output_file_or.value().path());
  if (writer_or.is_error()) {
    return writer_or.take_error_result();
  }
  auto writer = writer_or.take_value();

  uint64_t decompressed_bytes = 0;
  if (auto result = decompressor.Prepare([&decompressed_bytes, &writer](auto decompressed_data)
                                             -> fpromise::result<void, std::string> {
        if (auto result = writer.Write(decompressed_bytes, decompressed_data); result.is_error()) {
          return result;
        }
        decompressed_bytes += decompressed_data.size();
        return fpromise::ok();
      });
      result.is_error()) {
    return result;
  }

  std::vector<uint8_t> read_buffer;
  read_buffer.resize(1 << 20, 0);
  uint64_t read_bytes = 0;
  uint64_t hint = read_buffer.size();

  while (read_bytes < reader.length()) {
    uint64_t view_size = read_buffer.size();
    if (view_size > reader.length() - read_bytes) {
      view_size = reader.length() - read_bytes;
    }
    if (view_size > hint) {
      view_size = hint;
    }
    decompressor.ProvideSizeHint(view_size);
    auto read_view = cpp20::span<uint8_t>(read_buffer).subspan(0, view_size);

    if (auto result = reader.Read(read_bytes, read_view); result.is_error()) {
      return result;
    }

    auto decompress_result = decompressor.Decompress(read_view);
    if (decompress_result.is_error()) {
      return decompress_result.take_error_result();
    }

    // How much was actually consumed from the input buffer.
    auto [result_hint, consumed_bytes] = decompress_result.value();
    read_bytes += consumed_bytes;

    if (hint == 0) {
      break;
    }
    hint = result_hint;
  }
  if (auto result = decompressor.Finalize(); result.is_error()) {
    return result;
  }

  if (rename(output_file_or.value().path().data(), path.data()) == -1) {
    return fpromise::error(
        "Failed to move decompressed data to final destination. More specifically: " +
        std::string(strerror(errno)));
  }

  return fpromise::ok();
}

TEST(CreateCommandTest, CreateCompressedFvmBlockImageIsOk) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  CreateParams params = MakeParams();
  params.output_path = output_file_or.value().path();
  params.format = FvmImageFormat::kBlockImage;
  params.fvm_options.compression.schema = CompressionSchema::kLz4;

  auto create_result = Create(params);
  ASSERT_TRUE(create_result.is_ok()) << create_result.error();

  auto result = Decompress(output_file_or.value().path());
  ASSERT_TRUE(result.is_ok()) << result.error();

  fbl::unique_fd fvm_fd(open(output_file_or.value().path().data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());

  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());
}

TEST(CreateCommandTest, CreateNonCompressedFvmSpareImageIsOk) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  CreateParams params = MakeParams();
  params.output_path = output_file_or.value().path();
  params.format = FvmImageFormat::kSparseImage;
  params.fvm_options.compression.schema = CompressionSchema::kNone;

  // Add an empty partition.
  PartitionParams empty_partition;
  empty_partition.format = PartitionImageFormat::kEmptyPartition;
  empty_partition.label = "my-empty-partition";
  empty_partition.encrypted = false;
  empty_partition.options.max_bytes = 1;
  params.partitions.push_back(empty_partition);

  for (auto& partition_param : params.partitions) {
    if (partition_param.format == PartitionImageFormat::kBlobfs) {
      partition_param.encrypted = false;
    }
    if (partition_param.format == PartitionImageFormat::kMinfs) {
      partition_param.encrypted = true;
    }
  }

  ASSERT_TRUE(Create(params).is_ok());

  auto fvm_reader_or = FdReader::Create(output_file_or.value().path());
  ASSERT_TRUE(fvm_reader_or.is_ok()) << fvm_reader_or.error();
  std::unique_ptr<Reader> fvm_reader = std::make_unique<FdReader>(fvm_reader_or.take_value());

  auto descriptor_or = FvmSparseReadImage(0, std::move(fvm_reader));
  ASSERT_TRUE(descriptor_or.is_ok()) << descriptor_or.error();

  // Check that minfs is flagged as encrypted and that blobfs is not.
  // This is set as the default params.
  int checked_count = 0;
  for (const auto& partition : descriptor_or.value().partitions()) {
    if (partition.volume().name == "blobfs") {
      ASSERT_EQ(partition.volume().encryption, EncryptionType::kNone);
      checked_count++;
      continue;
    }

    if (partition.volume().name == "data") {
      ASSERT_EQ(partition.volume().encryption, EncryptionType::kZxcrypt);
      checked_count++;
      continue;
    }

    if (partition.volume().name == "my-empty-partition") {
      ASSERT_EQ(partition.volume().encryption, EncryptionType::kNone);
      checked_count++;
      continue;
    }
  }

  EXPECT_EQ(checked_count, 3);
}

TEST(CreateCommandTest, CreateNonCompressedFvmSpareImageWithNonEncryptedPartitionsIsOk) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  CreateParams params = MakeParams();
  params.output_path = output_file_or.value().path();
  params.format = FvmImageFormat::kSparseImage;
  params.fvm_options.compression.schema = CompressionSchema::kNone;

  for (auto& partition_param : params.partitions) {
    if (partition_param.format == PartitionImageFormat::kBlobfs) {
      partition_param.encrypted = false;
    }
    if (partition_param.format == PartitionImageFormat::kMinfs) {
      partition_param.encrypted = false;
    }
  }

  ASSERT_TRUE(Create(params).is_ok());

  auto fvm_reader_or = FdReader::Create(output_file_or.value().path());
  ASSERT_TRUE(fvm_reader_or.is_ok()) << fvm_reader_or.error();
  std::unique_ptr<Reader> fvm_reader = std::make_unique<FdReader>(fvm_reader_or.take_value());

  auto descriptor_or = FvmSparseReadImage(0, std::move(fvm_reader));
  ASSERT_TRUE(descriptor_or.is_ok()) << descriptor_or.error();

  // Check that minfs is flagged as encrypted and that blobfs is not.
  // This is set as the default params.
  EXPECT_EQ(descriptor_or.value().partitions().size(), 2u);
  for (const auto& partition : descriptor_or.value().partitions()) {
    ASSERT_EQ(partition.volume().encryption, EncryptionType::kNone);
  }
}

TEST(CreateCommandTest, CreateCompressedFvmSpareImageIsOk) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  CreateParams params = MakeParams();
  params.output_path = output_file_or.value().path();
  params.format = FvmImageFormat::kSparseImage;
  params.fvm_options.compression.schema = CompressionSchema::kLz4;

  for (auto& partition_param : params.partitions) {
    if (partition_param.format == PartitionImageFormat::kBlobfs) {
      partition_param.encrypted = false;
    }
    if (partition_param.format == PartitionImageFormat::kMinfs) {
      partition_param.encrypted = true;
    }
  }

  ASSERT_TRUE(Create(params).is_ok());

  auto fvm_reader_or = FdReader::Create(output_file_or.value().path());
  ASSERT_TRUE(fvm_reader_or.is_ok()) << fvm_reader_or.error();
  std::unique_ptr<Reader> fvm_reader = std::make_unique<FdReader>(fvm_reader_or.take_value());

  auto decompressed_image_or = TempFile::Create();
  ASSERT_TRUE(decompressed_image_or.is_ok());
  auto fvm_writer_or = FdWriter::Create(decompressed_image_or.value().path());
  ASSERT_TRUE(fvm_writer_or.is_ok()) << fvm_writer_or.error();
  FdWriter fvm_writer = fvm_writer_or.take_value();

  auto decompressed_or = FvmSparseDecompressImage(0, *fvm_reader, fvm_writer);
  ASSERT_TRUE(decompressed_or.is_ok()) << decompressed_or.error();

  // Should be a compressed image.
  ASSERT_TRUE(decompressed_or.value());

  auto decompresed_fvm_reader_or = FdReader::Create(decompressed_image_or.value().path());
  ASSERT_TRUE(decompresed_fvm_reader_or.is_ok()) << decompresed_fvm_reader_or.error();
  std::unique_ptr<Reader> decompresed_fvm_reader =
      std::make_unique<FdReader>(decompresed_fvm_reader_or.take_value());

  auto descriptor_or = FvmSparseReadImage(0, std::move(decompresed_fvm_reader));
  ASSERT_TRUE(descriptor_or.is_ok()) << descriptor_or.error();

  // Check that minfs is flagged as encrypted and that blobfs is not.
  // This is set as the default params.
  int checked_count = 0;
  for (const auto& partition : descriptor_or.value().partitions()) {
    if (partition.volume().name == "blobfs") {
      ASSERT_EQ(partition.volume().encryption, EncryptionType::kNone);
      checked_count++;
      continue;
    }

    if (partition.volume().name == "data") {
      ASSERT_EQ(partition.volume().encryption, EncryptionType::kZxcrypt);
      checked_count++;
      continue;
    }
  }

  EXPECT_EQ(checked_count, 2);
}

TEST(CreateCommandTest, CreateEmbeddedFvmImageIsOk) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  CreateParams params = MakeParams();
  params.output_path = output_file_or.value().path();
  params.format = FvmImageFormat::kBlockImage;
  params.fvm_options.compression.schema = CompressionSchema::kNone;
  params.is_output_embedded = true;
  params.offset = kInitialImageSize / 2;
  params.length = kInitialImageSize;

  ASSERT_EQ(truncate(std::string(output_file_or.value().path()).c_str(), 2 * kInitialImageSize), 0);

  for (auto& partition_param : params.partitions) {
    if (partition_param.format == PartitionImageFormat::kBlobfs) {
      partition_param.encrypted = false;
    }
    if (partition_param.format == PartitionImageFormat::kMinfs) {
      partition_param.encrypted = false;
    }
  }

  // Add poison values before the offset and after the length to verify afterwards.
  std::array<uint8_t, 10> canary = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  auto output_writer_or = FdWriter::Create(params.output_path);
  ASSERT_TRUE(output_writer_or.is_ok());
  ASSERT_TRUE(output_writer_or.value().Write(params.offset.value() - canary.size(), canary));
  ASSERT_TRUE(
      output_writer_or.value().Write(params.offset.value() + params.length.value(), canary));

  ASSERT_TRUE(Create(params).is_ok());

  // Copy the range into a new file and do fvm check on it.
  // Also check that the beginning and end are zeroes.
  auto fvm_reader_or = FdReader::Create(output_file_or.value().path());
  ASSERT_TRUE(fvm_reader_or.is_ok()) << fvm_reader_or.error();
  std::unique_ptr<Reader> fvm_reader = std::make_unique<FdReader>(fvm_reader_or.take_value());

  // Check canaries
  std::array<uint8_t, 10> canary_buffer;
  ASSERT_TRUE(fvm_reader->Read(params.offset.value() - canary.size(), canary_buffer));
  ASSERT_TRUE(memcmp(canary_buffer.data(), canary.data(), canary.size()) == 0);

  ASSERT_TRUE(fvm_reader->Read(params.offset.value() + params.length.value(), canary_buffer));
  ASSERT_TRUE(memcmp(canary_buffer.data(), canary.data(), canary.size()) == 0);

  uint64_t current_offset = params.offset.value();
  std::vector<uint8_t> buffer;
  buffer.resize(1 << 10, 0);

  auto copy_file_or = TempFile::Create();
  ASSERT_TRUE(copy_file_or.is_ok());

  auto fvm_copy_or = FdWriter::Create(copy_file_or.value().path());
  ASSERT_TRUE(fvm_copy_or.is_ok()) << fvm_copy_or.error();
  std::unique_ptr<FdWriter> fvm_copy = std::make_unique<FdWriter>(fvm_copy_or.take_value());

  // Copy contents into a new file to run fvm check.
  while (current_offset < params.offset.value() + params.length.value()) {
    auto buffer_view = cpp20::span<uint8_t>(buffer).subspan(
        0, std::min(params.offset.value() + params.length.value() - current_offset,
                    static_cast<uint64_t>(buffer.size())));
    ;
    ASSERT_TRUE(fvm_reader->Read(current_offset, buffer_view).is_ok());

    ASSERT_TRUE(fvm_copy->Write(current_offset - params.offset.value(), buffer_view).is_ok());
    current_offset += buffer_view.size();
  }

  fbl::unique_fd fvm_fd(open(copy_file_or.value().path().data(), O_RDONLY));
  ASSERT_TRUE(fvm_fd.is_valid());

  fvm::Checker fvm_checker(std::move(fvm_fd), 8 * (1 << 10), true);
  ASSERT_TRUE(fvm_checker.Validate());
}

TEST(CreateCommandTest, TooBigEmbeddedFvmImageIsError) {
  auto output_file_or = TempFile::Create();
  ASSERT_TRUE(output_file_or.is_ok());

  CreateParams params = MakeParams();
  params.output_path = output_file_or.value().path();
  params.format = FvmImageFormat::kBlockImage;
  params.fvm_options.compression.schema = CompressionSchema::kNone;
  params.is_output_embedded = true;
  params.offset = kInitialImageSize / 2;
  params.length = 1;

  ASSERT_EQ(truncate(std::string(output_file_or.value().path()).c_str(), 2 * kInitialImageSize), 0);

  for (auto& partition_param : params.partitions) {
    if (partition_param.format == PartitionImageFormat::kBlobfs) {
      partition_param.encrypted = false;
    }
    if (partition_param.format == PartitionImageFormat::kMinfs) {
      partition_param.encrypted = false;
    }
  }

  // Add poison values before the offset and after the length to verify afterwards.
  auto output_writer_or = FdWriter::Create(params.output_path);
  ASSERT_TRUE(output_writer_or.is_ok());

  ASSERT_TRUE(Create(params).is_error());
}

}  // namespace
}  // namespace storage::volume_image
