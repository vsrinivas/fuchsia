// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/result.h>
#include <string.h>
#include <unistd.h>

#include <charconv>
#include <cstdint>
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
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/partition.h"
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

  fit::result<void, std::string> Read(uint64_t offset, fbl::Span<uint8_t> buffer) const final {
    memset(buffer.data(), '0', buffer.size());
    return fit::ok();
  }
};

class BoundedWriter final : public Writer {
 public:
  BoundedWriter(std::unique_ptr<Writer> writer, uint64_t offset, uint64_t length)
      : offset_(offset), length_(length), writer_(std::move(writer)) {}

  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> buffer) final {
    if (offset + buffer.size() > length_) {
      return fit::error("BoundedWriter::Write out of bounds. offset: " + std::to_string(offset) +
                        " byte_cout: " + std::to_string(buffer.size()) +
                        " max_size: " + std::to_string(length_) + ".");
    }
    return writer_->Write(offset_ + offset, buffer);
  }

 private:
  uint64_t offset_ = 0;
  uint64_t length_ = 0;
  std::unique_ptr<Writer> writer_;
};

fit::result<Partition, std::string> ProcessPartition(const PartitionParams& params,
                                                     const FvmOptions& fvm_options) {
  Partition partition;

  auto volume_reader_or = FdReader::Create(params.source_image_path);
  if (volume_reader_or.is_error()) {
    return volume_reader_or.take_error_result();
  }
  std::unique_ptr<Reader> volume_reader = std::make_unique<FdReader>(volume_reader_or.take_value());

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
      return fit::error("Unknown Partition format.");
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

  return fit::ok(std::move(partition));
}
}  // namespace

fit::result<void, std::string> Create(const CreateParams& params) {
  if (params.output_path.empty()) {
    return fit::error("No image output path provided for Create.");
  }

  if (params.is_output_embedded) {
    if (!params.offset.has_value()) {
      return fit::error("Must provide offset for embedding fvm image.");
    }

    if (!params.length.has_value()) {
      return fit::error("Must provide length for embedding fvm image.");
    }
  }

  if (params.fvm_options.compression.schema != CompressionSchema::kNone &&
      params.format != FvmImageFormat::kSparseImage) {
    return fit::error("Compression is only supported for Sparse FVM Image format.");
  }

  if (params.fvm_options.slice_size == 0) {
    return fit::error("Slice size must be greater than zero.");
  }

  if (params.fvm_options.slice_size % fvm::kBlockSize != 0) {
    return fit::error("Slice size must be a multiple of fvm's block size(" +
                      std::to_string(fvm::kBlockSize >> 10) + " KB).");
  }

  fbl::unique_fd output_fd(open(params.output_path.c_str(), O_CREAT | O_WRONLY));
  if (!output_fd.is_valid()) {
    return fit::error("Opening output file failed. More specifically: " + Errno() + ".");
  }

  // If is not embedded then truncate the file.
  if (!params.is_output_embedded && params.fvm_options.target_volume_size.has_value()) {
    if (ftruncate(output_fd.get(), params.fvm_options.target_volume_size.value()) == -1) {
      return fit::error("Failed to truncate " + params.output_path + " to length " +
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
    if (params.format == FvmImageFormat::kBlockImage &&
        (partition_param.encrypted ||
         partition_or.value().volume().encryption != EncryptionType::kNone)) {
      return fit::error("FVM Block Image does not support encrypted partitions. Partition: \n" +
                        partition_or.value().volume().DebugString() + ".");
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
      return descriptor.WriteBlockImage(*writer);

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

  return fit::ok();
}

}  // namespace storage::volume_image
