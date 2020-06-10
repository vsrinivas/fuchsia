// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_sparse_image.h"

#include <lib/fit/result.h>

#include <cstdint>

#include <fbl/algorithm.h>
#include <fvm/fvm-sparse.h>

#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/extent.h"

namespace storage::volume_image {
namespace fvm_sparse_internal {

uint32_t GetImageFlags(const FvmOptions& options) {
  uint32_t flags = 0;
  switch (options.compression.schema) {
    case CompressionSchema::kLz4:
      flags |= fvm::kSparseFlagLz4;
      break;
    case CompressionSchema::kNone:
      flags &= ~fvm::kSparseFlagLz4;
      break;
    default:
      break;
  }
  return flags;
}

uint32_t GetPartitionFlags(const Partition& partition) {
  uint32_t flags = 0;

  switch (partition.volume().encryption) {
    case EncryptionType::kZxcrypt:
      flags |= fvm::kSparseFlagZxcrypt;
      break;
    case EncryptionType::kNone:
      flags &= ~fvm::kSparseFlagZxcrypt;
      break;
    default:
      break;
  }

  return flags;
}

}  // namespace fvm_sparse_internal

namespace {

// Dedicated memory for reading to and from the underlying media.
constexpr uint64_t kReadBufferSize = 4096;

// Returns a byte view of a fixed size struct.
// Currently we are not endian safe, so we are no worst than before. If this matter,
// this should be updated.
template <typename T>
fbl::Span<const uint8_t> FixedSizeStructToSpan(const T& typed_content) {
  return fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(&typed_content), sizeof(T));
}
}  // namespace

fvm::sparse_image_t FvmSparseGenerateHeader(const FvmDescriptor& descriptor) {
  fvm::sparse_image_t sparse_image_header = {};
  sparse_image_header.magic = fvm::kSparseFormatMagic;
  sparse_image_header.version = fvm::kSparseFormatVersion;
  sparse_image_header.slice_size = descriptor.options().slice_size;
  sparse_image_header.partition_count = descriptor.partitions().size();
  sparse_image_header.maximum_disk_size = descriptor.options().max_volume_size.value_or(0);
  sparse_image_header.flags = fvm_sparse_internal::GetImageFlags(descriptor.options());

  unsigned int extent_count = 0;
  for (const auto& partition : descriptor.partitions()) {
    extent_count += partition.address().mappings.size();
  }
  sparse_image_header.header_length =
      sizeof(fvm::partition_descriptor_t) * descriptor.partitions().size() +
      sizeof(fvm::extent_descriptor_t) * extent_count + sizeof(fvm::sparse_image_t);

  return sparse_image_header;
}

FvmSparsePartitionEntry FvmSparseGeneratePartitionEntry(uint64_t slice_size,
                                                        const Partition& partition) {
  FvmSparsePartitionEntry partition_entry = {};

  partition_entry.descriptor.magic = fvm::kPartitionDescriptorMagic;
  memcpy(partition_entry.descriptor.name, partition.volume().name.data(),
         partition.volume().name.size());
  memcpy(partition_entry.descriptor.type, partition.volume().type.data(),
         partition.volume().type.size());
  // TODO(gevalentino): Propagate instance guid, needs support from the sparse format.
  partition_entry.descriptor.extent_count = partition.address().mappings.size();
  partition_entry.descriptor.flags = fvm_sparse_internal::GetPartitionFlags(partition);

  for (const auto& mapping : partition.address().mappings) {
    Extent extent(mapping.source, mapping.count, partition.volume().block_size);
    auto [slice_extents, tail] = extent.Convert(mapping.target, slice_size);
    fvm::extent_descriptor_t extent_entry = {};
    extent_entry.magic = fvm::kExtentDescriptorMagic;
    extent_entry.slice_start = slice_extents.offset();
    extent_entry.slice_count = slice_extents.count();
    extent_entry.extent_length = slice_extents.count() * slice_extents.block_size() - tail.count;
    partition_entry.extents.push_back(extent_entry);
  }

  return partition_entry;
}

fit::result<void, std::string> FvmSparseWriteImage(const FvmDescriptor& descriptor,
                                                   Writer* writer) {
  uint64_t current_offset = 0;

  // Write the header.
  fvm::sparse_image_t header = FvmSparseGenerateHeader(descriptor);
  auto result = writer->Write(current_offset, FixedSizeStructToSpan(header));
  if (!result.empty()) {
    return fit::error(result);
  }
  current_offset += sizeof(fvm::sparse_image_t);

  for (const auto& partition : descriptor.partitions()) {
    FvmSparsePartitionEntry entry =
        FvmSparseGeneratePartitionEntry(descriptor.options().slice_size, partition);
    auto partition_result = writer->Write(current_offset, FixedSizeStructToSpan(entry.descriptor));
    if (!partition_result.empty()) {
      return fit::error(partition_result);
    }
    current_offset += sizeof(fvm::partition_descriptor_t);

    for (const auto& extent : entry.extents) {
      auto extent_result = writer->Write(current_offset, FixedSizeStructToSpan(extent));
      if (!extent_result.empty()) {
        return fit::error(extent_result);
      }
      current_offset += sizeof(fvm::extent_descriptor_t);
    }
  }

  if (current_offset != header.header_length) {
    return fit::error("fvm::sparse_image_t data does not start at header_length.");
  }

  // TODO(gevalentino): In order to add compression support of the partition data, we need to
  // provide a compression context for this entire chunk.
  std::vector<uint8_t> data(kReadBufferSize, 0);
  for (const auto& partition : descriptor.partitions()) {
    const auto* reader = partition.reader();
    for (const auto& mapping : partition.address().mappings) {
      uint64_t remaining_bytes = mapping.count * partition.volume().block_size;

      memset(data.data(), 0, data.size());

      uint64_t read_offset = mapping.source * partition.volume().block_size;
      while (remaining_bytes > 0) {
        uint64_t bytes_to_read = std::min(kReadBufferSize, remaining_bytes);
        remaining_bytes -= bytes_to_read;
        auto buffer_view = fbl::Span(data.data(), bytes_to_read);

        std::string extent_data_read_error = reader->Read(read_offset, buffer_view);
        if (!extent_data_read_error.empty()) {
          return fit::error(extent_data_read_error);
        }
        read_offset += bytes_to_read;

        std::string extent_data_write_error = writer->Write(current_offset, buffer_view);
        if (!extent_data_write_error.empty()) {
          return fit::error(extent_data_write_error);
        }
        current_offset += bytes_to_read;
      }
    }
  }
  return fit::ok();
}

uint64_t FvmSparseCalculateImageSize(const FvmDescriptor& descriptor) {
  uint64_t image_size = sizeof(fvm::sparse_image_t);

  for (const auto& partition : descriptor.partitions()) {
    image_size += sizeof(fvm::partition_descriptor_t);
    for (const auto& mapping : partition.address().mappings) {
      // Account for extent size, in the current format trailing zeroes are omitted,
      // and later filled as the difference between extent_length and slice_count * slice_size.
      image_size += partition.volume().block_size * mapping.count;
      // Extent descriptor size.
      image_size += sizeof(fvm::extent_descriptor_t);
    }
  }

  return image_size;
}

}  // namespace storage::volume_image
