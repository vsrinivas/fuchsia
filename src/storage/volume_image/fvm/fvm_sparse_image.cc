// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_sparse_image.h"

#include <lib/fit/result.h>

#include <bitset>
#include <cstdint>
#include <string>

#include <fbl/algorithm.h>
#include <fvm/format.h>
#include <fvm/fvm-sparse.h>

#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/compressor.h"

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

template <typename T>
fbl::Span<uint8_t> FixedSizeStructToSpan(T& typed_content) {
  return fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(&typed_content), sizeof(T));
}

class NoopCompressor final : public Compressor {
 public:
  fit::result<void, std::string> Prepare(Handler handler) final {
    handler_ = std::move(handler);
    return fit::ok();
  }

  fit::result<void, std::string> Compress(fbl::Span<const uint8_t> uncompressed_data) final {
    handler_(uncompressed_data);
    return fit::ok();
  }

  fit::result<void, std::string> Finalize() final { return fit::ok(); }

 private:
  Handler handler_ = nullptr;
};

fit::result<uint64_t, std::string> FvmSparseWriteImageInternal(const FvmDescriptor& descriptor,
                                                               Writer* writer,
                                                               Compressor* compressor) {
  uint64_t current_offset = 0;

  // Write the header.
  fvm::SparseImage header = FvmSparseGenerateHeader(descriptor);
  auto result = writer->Write(current_offset, FixedSizeStructToSpan(header));
  if (result.is_error()) {
    return result.take_error_result();
  }
  current_offset += sizeof(fvm::SparseImage);

  for (const auto& partition : descriptor.partitions()) {
    auto partition_entry_result =
        FvmSparseGeneratePartitionEntry(descriptor.options().slice_size, partition);
    if (partition_entry_result.is_error()) {
      return partition_entry_result.take_error_result();
    }

    FvmSparsePartitionEntry entry = partition_entry_result.take_value();
    auto partition_result = writer->Write(current_offset, FixedSizeStructToSpan(entry.descriptor));
    if (partition_result.is_error()) {
      return partition_result.take_error_result();
    }
    current_offset += sizeof(fvm::PartitionDescriptor);

    for (const auto& extent : entry.extents) {
      auto extent_result = writer->Write(current_offset, FixedSizeStructToSpan(extent));
      if (extent_result.is_error()) {
        return extent_result.take_error_result();
      }
      current_offset += sizeof(fvm::ExtentDescriptor);
    }
  }

  if (current_offset != header.header_length) {
    return fit::error("fvm::SparseImage data does not start at header_length.");
  }

  std::vector<uint8_t> data(kReadBufferSize, 0);
  compressor->Prepare(
      [&current_offset, writer](auto compressed_data) -> fit::result<void, std::string> {
        auto extent_data_write_result = writer->Write(current_offset, compressed_data);
        if (extent_data_write_result.is_error()) {
          return extent_data_write_result.take_error_result();
        }
        current_offset += compressed_data.size();
        return fit::ok();
      });
  for (const auto& partition : descriptor.partitions()) {
    const auto* reader = partition.reader();
    for (const auto& mapping : partition.address().mappings) {
      uint64_t remaining_bytes = mapping.count;

      memset(data.data(), 0, data.size());

      uint64_t read_offset = mapping.source;
      while (remaining_bytes > 0) {
        uint64_t bytes_to_read = std::min(kReadBufferSize, remaining_bytes);
        remaining_bytes -= bytes_to_read;
        auto buffer_view = fbl::Span(data.data(), bytes_to_read);

        auto extent_data_read_result = reader->Read(read_offset, buffer_view);
        if (extent_data_read_result.is_error()) {
          return extent_data_read_result.take_error_result();
        }
        read_offset += bytes_to_read;

        auto compress_result = compressor->Compress(buffer_view);
        if (compress_result.is_error()) {
          return fit::error(compress_result.take_error());
        }
      }
    }
  }
  auto finalize_result = compressor->Finalize();
  if (finalize_result.is_error()) {
    return finalize_result.take_error_result();
  }

  // |current_offset| now contains the total written bytes.
  return fit::ok(current_offset);
}

bool AddRange(std::map<uint64_t, uint64_t>& existing_ranges, uint64_t start, uint64_t length) {
  auto end = start + length;
  for (auto [cur_start, cur_end] : existing_ranges) {
    // disjoint sets dont overlap.
    if (cur_end > start && cur_start < end) {
      return false;
    }
  }
  existing_ranges[start] = start + length;
  return true;
}

}  // namespace

fvm::SparseImage FvmSparseGenerateHeader(const FvmDescriptor& descriptor) {
  fvm::SparseImage sparse_image_header = {};
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
      sizeof(fvm::PartitionDescriptor) * descriptor.partitions().size() +
      sizeof(fvm::ExtentDescriptor) * extent_count + sizeof(fvm::SparseImage);

  return sparse_image_header;
}

fit::result<FvmSparsePartitionEntry, std::string> FvmSparseGeneratePartitionEntry(
    uint64_t slice_size, const Partition& partition) {
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
    uint64_t size = std::max(mapping.count, mapping.size.value_or(0));
    uint64_t slice_count = GetBlockCount(mapping.target, size, slice_size);
    uint64_t slice_offset = GetBlockFromBytes(mapping.target, slice_size);
    if (!IsOffsetBlockAligned(mapping.target, slice_size)) {
      return fit::error("Partition " + partition.volume().name + " contains unaligned mapping " +
                        std::to_string(mapping.target) +
                        ". FVM Sparse Image requires slice aligned extent |vslice_start|.");
    }

    fvm::ExtentDescriptor extent_entry = {};
    extent_entry.magic = fvm::kExtentDescriptorMagic;
    extent_entry.slice_start = slice_offset;
    extent_entry.slice_count = slice_count;
    extent_entry.extent_length = mapping.count;
    partition_entry.extents.push_back(extent_entry);
  }

  return fit::ok(partition_entry);
}

fit::result<uint64_t, std::string> FvmSparseWriteImage(const FvmDescriptor& descriptor,
                                                       Writer* writer, Compressor* compressor) {
  if (compressor == nullptr) {
    NoopCompressor noop_compressor;
    return FvmSparseWriteImageInternal(descriptor, writer, &noop_compressor);
  }
  return FvmSparseWriteImageInternal(descriptor, writer, compressor);
}

uint64_t FvmSparseCalculateUncompressedImageSize(const FvmDescriptor& descriptor) {
  uint64_t image_size = sizeof(fvm::SparseImage);

  for (const auto& partition : descriptor.partitions()) {
    image_size += sizeof(fvm::PartitionDescriptor);
    for (const auto& mapping : partition.address().mappings) {
      // Account for extent size, in the current format trailing zeroes are omitted,
      // and later filled as the difference between extent_length and slice_count * slice_size.
      image_size += mapping.count;
      // Extent descriptor size.
      image_size += sizeof(fvm::ExtentDescriptor);
    }
  }

  return image_size;
}

fit::result<fvm::SparseImage, std::string> FvmSparseImageGetHeader(uint64_t offset,
                                                                   const Reader& reader) {
  fvm::SparseImage header = {};
  auto header_buffer = FixedSizeStructToSpan(header);

  auto header_read_result = reader.Read(offset, header_buffer);
  if (header_read_result.is_error()) {
    return header_read_result.take_error_result();
  }

  if (header.magic != fvm::kSparseFormatMagic) {
    return fit::error("Fvm Sparse Image header |magic| is incorrect. Expected " +
                      std::to_string(fvm::kSparseFormatMagic) + ", but found " +
                      std::to_string(header.magic) + ".");
  }

  if (header.version != fvm::kSparseFormatVersion) {
    return fit::error("Fvm Sparse Image header |version| is incorrect. Expected " +
                      std::to_string(fvm::kSparseFormatVersion) + ", but found " +
                      std::to_string(header.version) + ".");
  }

  if ((header.flags & ~fvm::kSparseFlagAllValid) != 0) {
    return fit::error(
        "Fvm Sparse Image header |flags| contains invalid values. Found " +
        std::bitset<sizeof(fvm::SparseImage::flags)>(header.flags).to_string() + " valid flags " +
        std::bitset<sizeof(fvm::SparseImage::flags)>(fvm::kSparseFlagAllValid).to_string());
  }

  if (header.header_length < sizeof(fvm::SparseImage)) {
    return fit::error("Fvm Sparse Image header |header_length| must be at least " +
                      std::to_string(sizeof(fvm::SparseImage)) + ", but was " +
                      std::to_string(header.header_length) + ".");
  }

  if (header.slice_size == 0) {
    return fit::error("Fvm Sparse Image header |slice_size| must be non zero.");
  }

  return fit::ok(header);
}

fit::result<std::vector<FvmSparsePartitionEntry>, std::string> FvmSparseImageGetPartitions(
    uint64_t offset, const Reader& reader, const fvm::SparseImage& header) {
  std::vector<FvmSparsePartitionEntry> partitions(header.partition_count);
  uint64_t current_offset = offset;

  // Check partitions and extents.
  for (uint64_t i = 0; i < header.partition_count; ++i) {
    FvmSparsePartitionEntry& partition = partitions[i];
    auto partition_read_result =
        reader.Read(current_offset, FixedSizeStructToSpan(partition.descriptor));
    if (partition_read_result.is_error()) {
      return partition_read_result.take_error_result();
    }

    if (partition.descriptor.magic != fvm::kPartitionDescriptorMagic) {
      return fit::error(
          "Fvm Sparse Image Partition descriptor contains incorrect magic. Expected " +
          std::to_string(fvm::kPartitionDescriptorMagic) + ", but found " +
          std::to_string(partition.descriptor.magic) + ".");
    }

    if ((partition.descriptor.flags & ~fvm::kSparseFlagAllValid) != 0) {
      return fit::error("Fvm Sparse Image Partition descriptor contains unknown flags.");
    }

    current_offset += sizeof(fvm::PartitionDescriptor);
    std::map<uint64_t, uint64_t> allocated_ranges;
    for (uint32_t j = 0; j < partition.descriptor.extent_count; ++j) {
      fvm::ExtentDescriptor extent = {};
      auto extent_read_result = reader.Read(current_offset, FixedSizeStructToSpan(extent));
      if (extent_read_result.is_error()) {
        return extent_read_result.take_error_result();
      }

      if (extent.magic != fvm::kExtentDescriptorMagic) {
        return fit::error("Fvm Sparse Image Partition " + std::to_string(i) +
                          " extent descriptor " + std::to_string(j) +
                          " contains invalid magic. Expected " +
                          std::to_string(fvm::kExtentDescriptorMagic) + ", but found " +
                          std::to_string(extent.magic) + ".");
      }

      if (extent.extent_length > extent.slice_count * header.slice_size) {
        return fit::error("Fvm Sparse Image Partition " + std::to_string(i) +
                          " extent descriptor " + std::to_string(j) + " extent length(" +
                          std::to_string(extent.extent_length) +
                          ") exceeds the allocated slice range(" +
                          std::to_string(extent.slice_count * header.slice_size) + "), " +
                          std::to_string(extent.slice_count) + " allocated slices of size " +
                          std::to_string(header.slice_size) + ".");
      }

      if (!AddRange(allocated_ranges, extent.slice_start, extent.slice_count)) {
        return fit::error("Fvm Sparse Image Partition " + std::to_string(i) +
                          " extent descriptor " + std::to_string(j) +
                          " contains overlapping slice ranges.");
      }

      current_offset += sizeof(fvm::ExtentDescriptor);
      partition.extents.push_back(extent);
    }
  }

  return fit::ok(partitions);
}

CompressionOptions FvmSparseImageGetCompressionOptions(const fvm::SparseImage& header) {
  CompressionOptions options;
  options.schema = CompressionSchema::kNone;
  if ((header.flags & fvm::kSparseFlagLz4) != 0) {
    options.schema = CompressionSchema::kLz4;
  }

  return options;
}

}  // namespace storage::volume_image
