// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/result.h>
#include <string.h>
#include <unistd.h>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include <fbl/unique_fd.h>

#include "src/storage/fvm/format.h"
#include "src/storage/volume_image/adapter/blobfs_partition.h"
#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/adapter/empty_partition.h"
#include "src/storage/volume_image/adapter/minfs_partition.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_image_extend.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/partition.h"
#include "src/storage/volume_image/utils/bounded_writer.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/fd_writer.h"
#include "src/storage/volume_image/utils/lz4_compressor.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {

std::string Errno() { return std::string(strerror(errno)); }

class ZeroReader final : public Reader {
  uint64_t length() const final { return std::numeric_limits<uint64_t>::max(); }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    memset(buffer.data(), '0', buffer.size());
    return fpromise::ok();
  }
};

fpromise::result<Partition, std::string> ProcessPartition(const PartitionParams& params,
                                                          const FvmOptions& fvm_options) {
  Partition partition;

  std::unique_ptr<Reader> volume_reader;
  if (params.format != PartitionImageFormat::kEmptyPartition) {
    auto volume_reader_or = FdReader::Create(params.source_image_path);
    if (volume_reader_or.is_error()) {
      return volume_reader_or.take_error_result();
    }
    volume_reader = std::make_unique<FdReader>(volume_reader_or.take_value());
  }

  switch (params.format) {
    case PartitionImageFormat::kBlobfs: {
      auto partition_or =
          CreateBlobfsFvmPartition(std::move(volume_reader), params.options, fvm_options);
      if (partition_or.is_error()) {
        return partition_or.take_error_result();
      }
      partition = std::move(partition_or.value());
      break;
    }

    case PartitionImageFormat::kMinfs: {
      auto partition_or =
          CreateMinfsFvmPartition(std::move(volume_reader), params.options, fvm_options);
      if (partition_or.is_error()) {
        return partition_or.take_error_result();
      }
      partition = std::move(partition_or.value());
      break;
    }

    case PartitionImageFormat::kEmptyPartition: {
      auto partition_or = CreateEmptyFvmPartition(params.options, fvm_options);
      if (partition_or.is_error()) {
        return partition_or.take_error_result();
      }
      partition = partition_or.take_value();
    } break;

    default:
      return fpromise::error("Unknown Partition format.");
  }

  // At this point we have a default Minfs or Blobfs partition, but we need to adjust it.
  if (!params.label.empty()) {
    partition.volume().name = params.label;
  }

  if (params.type_guid.has_value()) {
    partition.volume().type = params.type_guid.value();
  }

  if (params.encrypted) {
    partition.volume().encryption = EncryptionType::kZxcrypt;
  } else {
    partition.volume().encryption = EncryptionType::kNone;
  }

  return fpromise::ok(std::move(partition));
}

fpromise::result<void, std::string> CompressFile(std::string_view input, std::string_view output) {
  auto input_reader_or = FdReader::Create(input);
  if (input_reader_or.is_error()) {
    return input_reader_or.take_error_result();
  }
  auto input_reader = input_reader_or.take_value();

  std::string output_tmp = std::string(output) + ".lz4.tmp";
  auto remove_temp_file = fit::defer([&output_tmp]() { unlink(output_tmp.c_str()); });
  // Clean up any existing remainders from previous run if it wasnt cleaned up properly.
  unlink(output_tmp.c_str());

  // Create a temporary file to compress into, just in case, input == output.
  fbl::unique_fd output_tmp_fd(open(output_tmp.c_str(), O_CREAT | O_WRONLY, 0644));
  if (!output_tmp_fd.is_valid()) {
    auto err = Errno();
    return fpromise::error("Failed to create temporary file at " + output_tmp +
                           "for decompression. More specifically: " + err + ".");
  }

  auto compression_writer_or = FdWriter::Create(output_tmp.c_str());
  if (compression_writer_or.is_error()) {
    return compression_writer_or.take_error_result();
  }
  auto compression_writer = compression_writer_or.take_value();

  // Now stream the contents of the input file.
  uint64_t read_bytes = 0;

  // 1 MB buffer size.
  constexpr uint64_t kMaxBufferSize = 1 << 20;
  std::vector<uint8_t> read_buffer;
  read_buffer.resize(kMaxBufferSize, 0);

  // Initialize the compressor
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  auto compressor_or = Lz4Compressor::Create(options);
  if (compressor_or.is_error()) {
    return compressor_or.take_error_result();
  }
  auto compressor = compressor_or.take_value();
  uint64_t written_bytes = 0;

  compressor.Prepare(
      [&compression_writer, &written_bytes](auto buffer) -> fpromise::result<void, std::string> {
        if (auto result = compression_writer.Write(written_bytes, buffer); result.is_error()) {
          return result;
        }
        written_bytes += buffer.size();
        return fpromise::ok();
      });
  while (read_bytes < input_reader.length()) {
    auto read_view =
        cpp20::span<uint8_t>(read_buffer)
            .subspan(0, std::min(kMaxBufferSize,
                                 static_cast<uint64_t>(input_reader.length() - read_bytes)));
    if (auto result = input_reader.Read(read_bytes, read_view); result.is_error()) {
      return result;
    }
    read_bytes += read_view.size();

    if (auto compress_result = compressor.Compress(read_view); compress_result.is_error()) {
      return compress_result;
    }
  }

  if (auto compress_result = compressor.Finalize(); compress_result.is_error()) {
    return compress_result;
  }

  // Move the temporary output into the primary one.
  if (auto result = rename(output_tmp.c_str(), std::string(output).c_str()); result == -1) {
    auto err = Errno();
    return fpromise::error("Failed to move temporary compressed file " + output_tmp +
                           " to final location " + std::string(output) +
                           ". More specifically: " + err + ".");
  }

  return fpromise::ok();
}

}  // namespace

fpromise::result<void, std::string> Create(const CreateParams& params) {
  if (params.output_path.empty()) {
    return fpromise::error("No image output path provided for Create.");
  }

  if (params.is_output_embedded) {
    if (!params.offset.has_value()) {
      return fpromise::error("Must provide offset for embedding fvm image.");
    }

    if (!params.length.has_value()) {
      return fpromise::error("Must provide length for embedding fvm image.");
    }
  }

  if (params.fvm_options.slice_size == 0) {
    return fpromise::error("Slice size must be greater than zero.");
  }

  if (params.fvm_options.slice_size % fvm::kBlockSize != 0) {
    return fpromise::error("Slice size must be a multiple of fvm's block size(" +
                           std::to_string(fvm::kBlockSize >> 10) + " KB).");
  }

  // When not embedded, clean up any existing file under such name.
  if (!params.is_output_embedded) {
    unlink(params.output_path.c_str());
  }

  fbl::unique_fd output_fd(open(params.output_path.c_str(), O_CREAT | O_WRONLY, 0644));
  if (!output_fd.is_valid()) {
    return fpromise::error("Opening output file failed. More specifically: " + Errno() + ".");
  }

  // If is not embedded then truncate the file.
  if (!params.is_output_embedded && params.fvm_options.target_volume_size.has_value()) {
    if (ftruncate(output_fd.get(), params.fvm_options.target_volume_size.value()) == -1) {
      return fpromise::error("Failed to truncate " + params.output_path + " to length " +
                             std::to_string(params.fvm_options.target_volume_size.value()) +
                             ". More specifically: " + Errno() + ".");
    }
  }

  std::unique_ptr<Writer> writer = std::make_unique<FdWriter>(std::move(output_fd));
  // If we are embedding somewhere, make it so the writers errors out if someone attempts
  // to write out of the designated area.
  if (params.is_output_embedded) {
    writer = std::make_unique<BoundedWriter>(std::move(writer), params.offset.value(),
                                             params.length.value());
  }

  FvmDescriptor::Builder builder;
  builder.SetOptions(params.fvm_options);

  for (const auto& partition_param : params.partitions) {
    auto partition_or = ProcessPartition(partition_param, params.fvm_options);
    if (partition_or.is_error()) {
      return partition_or.take_error_result();
    }
    builder.AddPartition(partition_or.take_value());
  }

  auto descriptor_or = builder.Build();
  if (descriptor_or.is_error()) {
    return descriptor_or.take_error_result();
  }
  auto descriptor = descriptor_or.take_value();

  switch (params.format) {
    case FvmImageFormat::kBlockImage:
      if (auto result = descriptor.WriteBlockImage(*writer); result.is_error()) {
        return result.take_error_result();
      }

      if (params.trim_image) {
        auto output_reader_or = FdReader::Create(params.output_path);
        if (output_reader_or.is_error()) {
          return output_reader_or.take_error_result();
        }
        // Calculate the trim size.
        auto trim_size_or = FvmImageGetTrimmedSize(output_reader_or.value());
        if (trim_size_or.is_error()) {
          return trim_size_or.take_error_result();
        }
        if (truncate(params.output_path.c_str(),
                     static_cast<off_t>(params.offset.value_or(0) + trim_size_or.value())) == -1) {
          return fpromise::error("Resize to fit image failed. Trimming " + params.output_path +
                                 " to length " + std::to_string(trim_size_or.value()) +
                                 ". More specifically: " + Errno() + ".");
        }
      }

      if (params.fvm_options.compression.schema != CompressionSchema::kNone) {
        return CompressFile(params.output_path, params.output_path);
      }

      return fpromise::ok();

    case FvmImageFormat::kSparseImage:
      std::unique_ptr<Compressor> compressor = nullptr;
      if (params.fvm_options.compression.schema != CompressionSchema::kNone) {
        auto compressor_or = Lz4Compressor::Create(params.fvm_options.compression);
        if (compressor_or.is_error()) {
          return compressor_or.take_error_result();
        }
        compressor = std::make_unique<Lz4Compressor>(compressor_or.take_value());
      }
      if (auto result = FvmSparseWriteImage(descriptor, writer.get(), compressor.get());
          result.is_error()) {
        return result.take_error_result();
      }
      break;
  }

  return fpromise::ok();
}

}  // namespace storage::volume_image
