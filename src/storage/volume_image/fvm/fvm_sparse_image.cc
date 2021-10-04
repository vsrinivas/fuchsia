// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_sparse_image.h"

#include <lib/fpromise/result.h>
#include <string.h>

#include <bitset>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <fbl/algorithm.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_sparse.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/compressor.h"
#include "src/storage/volume_image/utils/lz4_decompress_reader.h"
#include "src/storage/volume_image/utils/lz4_decompressor.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {
// Dedicated memory for reading to and from the underlying media.
constexpr uint64_t kReadBufferSize = 4096;

// Returns a byte view of a fixed size struct.
// Currently we are not endian safe, so we are no worst than before. If this matter,
// this should be updated.
template <typename T>
cpp20::span<const uint8_t> FixedSizeStructToSpan(const T& typed_content) {
  return cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&typed_content), sizeof(T));
}

template <typename T>
cpp20::span<uint8_t> FixedSizeStructToSpan(T& typed_content) {
  return cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&typed_content), sizeof(T));
}

class NoopCompressor final : public Compressor {
 public:
  fpromise::result<void, std::string> Prepare(Handler handler) final {
    handler_ = std::move(handler);
    return fpromise::ok();
  }

  fpromise::result<void, std::string> Compress(cpp20::span<const uint8_t> uncompressed_data) final {
    handler_(uncompressed_data);
    return fpromise::ok();
  }

  fpromise::result<void, std::string> Finalize() final { return fpromise::ok(); }

 private:
  Handler handler_ = nullptr;
};

fpromise::result<uint64_t, std::string> FvmSparseWriteImageInternal(const FvmDescriptor& descriptor,
                                                                    Writer* writer,
                                                                    Compressor* compressor) {
  uint64_t current_offset = 0;

  // Write the header.
  fvm::SparseImage header = fvm_sparse_internal::GenerateHeader(descriptor);
  bool default_fill_extents = (header.flags & fvm::kSparseFlagZeroFillNotRequired) != 0;

  auto result = writer->Write(current_offset, FixedSizeStructToSpan(header));
  if (result.is_error()) {
    return result.take_error_result();
  }
  current_offset += sizeof(fvm::SparseImage);

  for (const auto& partition : descriptor.partitions()) {
    auto partition_entry_result = fvm_sparse_internal::GeneratePartitionEntry(
        descriptor.options().slice_size, partition, default_fill_extents);
    if (partition_entry_result.is_error()) {
      return partition_entry_result.take_error_result();
    }

    fvm_sparse_internal::PartitionEntry entry = partition_entry_result.take_value();
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
    return fpromise::error("fvm::SparseImage data does not start at header_length.");
  }

  std::vector<uint8_t> data(kReadBufferSize, 0);
  compressor->Prepare(
      [&current_offset, writer](auto compressed_data) -> fpromise::result<void, std::string> {
        auto extent_data_write_result = writer->Write(current_offset, compressed_data);
        if (extent_data_write_result.is_error()) {
          return extent_data_write_result.take_error_result();
        }
        current_offset += compressed_data.size();
        return fpromise::ok();
      });
  for (const auto& partition : descriptor.partitions()) {
    const auto* reader = partition.reader();
    for (const auto& mapping : partition.address().mappings) {
      uint64_t remaining_bytes = mapping.count;
      uint64_t default_fill_remaining_bytes = 0;

      auto default_fill_value_it = mapping.options.find(EnumAsString(AddressMapOption::kFill));
      std::optional<uint8_t> default_fill_value;
      if (default_fill_extents && default_fill_value_it != mapping.options.end()) {
        uint64_t size = std::max(mapping.size.value_or(0), mapping.count);
        uint64_t slice_count = GetBlockCount(mapping.target, size, descriptor.options().slice_size);
        // Need to fill all the way up the slice boundary.
        default_fill_remaining_bytes =
            slice_count * descriptor.options().slice_size - mapping.count;
        default_fill_value = static_cast<uint8_t>(default_fill_value_it->second);
      }

      memset(data.data(), default_fill_value.value_or(0), data.size());

      uint64_t read_offset = mapping.source;
      while (remaining_bytes > 0) {
        uint64_t bytes_to_read = std::min(kReadBufferSize, remaining_bytes);
        remaining_bytes -= bytes_to_read;
        auto buffer_view = cpp20::span(data.data(), bytes_to_read);

        auto extent_data_read_result = reader->Read(read_offset, buffer_view);
        if (extent_data_read_result.is_error()) {
          return extent_data_read_result.take_error_result();
        }
        read_offset += bytes_to_read;

        auto compress_result = compressor->Compress(buffer_view);
        if (compress_result.is_error()) {
          return fpromise::error(compress_result.take_error());
        }
      }

      memset(data.data(), default_fill_value.value_or(0), data.size());
      while (default_fill_remaining_bytes > 0) {
        uint64_t bytes_to_write = std::min(kReadBufferSize, default_fill_remaining_bytes);
        default_fill_remaining_bytes -= bytes_to_write;
        auto buffer_view = cpp20::span(data.data(), bytes_to_write);

        auto compress_result = compressor->Compress(buffer_view);
        if (compress_result.is_error()) {
          return fpromise::error(compress_result.take_error());
        }
      }
    }
  }
  auto finalize_result = compressor->Finalize();
  if (finalize_result.is_error()) {
    return finalize_result.take_error_result();
  }

  // |current_offset| now contains the total written bytes.
  return fpromise::ok(current_offset);
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

// Reader implementation that shares ownership of a reader with other instances.
class SharedReader final : public Reader {
 public:
  SharedReader(uint64_t offset, uint64_t length, std::shared_ptr<Reader> image_reader)
      : offset_(offset), length_(length), image_reader_(std::move(image_reader)) {}

  uint64_t length() const final { return length_; }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (offset + buffer.size() > length_) {
      return fpromise::error("SharedReader::Read out of bounds. Offset: " + std::to_string(offset) +
                             " Length: " + std::to_string(buffer.size()) +
                             " Max Length: " + std::to_string(length_) + ".");
    }
    return image_reader_->Read(offset_ + offset, buffer);
  }

 private:
  uint64_t offset_ = 0;
  uint64_t length_ = 0;
  std::shared_ptr<Reader> image_reader_;
};

}  // namespace

namespace fvm_sparse_internal {

uint32_t GetImageFlags(const FvmOptions& options) {
  uint32_t flags = 0;
  switch (options.compression.schema) {
    case CompressionSchema::kLz4:
      flags |= fvm::kSparseFlagLz4;
      flags |= fvm::kSparseFlagZeroFillNotRequired;
      break;
    case CompressionSchema::kNone:
      flags &= ~fvm::kSparseFlagLz4;
      flags &= ~fvm::kSparseFlagZeroFillNotRequired;
      break;
    default:
      break;
  }
  return flags;
}

uint32_t GetPartitionFlags(const Partition& partition) {
  uint32_t flags = 0;

  // TODO(jfsulliv): Propagate all kSparseFlags.
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

  flags |= fvm::kSparseFlagZeroFillNotRequired;
  for (const auto& mapping : partition.address().mappings) {
    if (mapping.options.find(EnumAsString(AddressMapOption::kFill)) != mapping.options.end()) {
      flags &= ~fvm::kSparseFlagZeroFillNotRequired;
    }
  }

  return flags;
}

fvm::SparseImage GenerateHeader(const FvmDescriptor& descriptor) {
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

fpromise::result<PartitionEntry, std::string> GeneratePartitionEntry(uint64_t slice_size,
                                                                     const Partition& partition,
                                                                     bool extents_are_filled) {
  PartitionEntry partition_entry = {};

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
      return fpromise::error("Partition " + partition.volume().name +
                             " contains unaligned mapping " + std::to_string(mapping.target) +
                             ". FVM Sparse Image requires slice aligned extent |vslice_start|.");
    }

    fvm::ExtentDescriptor extent_entry = {};
    extent_entry.magic = fvm::kExtentDescriptorMagic;
    extent_entry.slice_start = slice_offset;
    extent_entry.slice_count = slice_count;
    extent_entry.extent_length = mapping.count;
    if (extents_are_filled &&
        (mapping.options.find(EnumAsString(AddressMapOption::kFill)) != mapping.options.end())) {
      extent_entry.extent_length = slice_count * slice_size;
    }
    partition_entry.extents.push_back(extent_entry);
  }

  return fpromise::ok(partition_entry);
}

uint64_t CalculateUncompressedImageSize(const FvmDescriptor& descriptor) {
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

fpromise::result<fvm::SparseImage, std::string> GetHeader(uint64_t offset, const Reader& reader) {
  fvm::SparseImage header = {};
  auto header_buffer = FixedSizeStructToSpan(header);

  auto header_read_result = reader.Read(offset, header_buffer);
  if (header_read_result.is_error()) {
    return header_read_result.take_error_result();
  }

  if (header.magic != fvm::kSparseFormatMagic) {
    return fpromise::error("Fvm Sparse Image header |magic| is incorrect. Expected " +
                           std::to_string(fvm::kSparseFormatMagic) + ", but found " +
                           std::to_string(header.magic) + ".");
  }

  if (header.version != fvm::kSparseFormatVersion) {
    return fpromise::error("Fvm Sparse Image header |version| is incorrect. Expected " +
                           std::to_string(fvm::kSparseFormatVersion) + ", but found " +
                           std::to_string(header.version) + ".");
  }

  if ((header.flags & ~fvm::kSparseFlagAllValid) != 0) {
    return fpromise::error(
        "Fvm Sparse Image header |flags| contains invalid values. Found " +
        std::bitset<sizeof(fvm::SparseImage::flags)>(header.flags).to_string() + " valid flags " +
        std::bitset<sizeof(fvm::SparseImage::flags)>(fvm::kSparseFlagAllValid).to_string());
  }

  if (header.header_length < sizeof(fvm::SparseImage)) {
    return fpromise::error("Fvm Sparse Image header |header_length| must be at least " +
                           std::to_string(sizeof(fvm::SparseImage)) + ", but was " +
                           std::to_string(header.header_length) + ".");
  }

  if (header.slice_size == 0) {
    return fpromise::error("Fvm Sparse Image header |slice_size| must be non zero.");
  }

  return fpromise::ok(header);
}

fpromise::result<std::vector<PartitionEntry>, std::string> GetPartitions(
    uint64_t offset, const Reader& reader, const fvm::SparseImage& header) {
  std::vector<PartitionEntry> partitions(header.partition_count);
  uint64_t current_offset = offset;

  // Check partitions and extents.
  for (uint64_t i = 0; i < header.partition_count; ++i) {
    PartitionEntry& partition = partitions[i];
    auto partition_read_result =
        reader.Read(current_offset, FixedSizeStructToSpan(partition.descriptor));
    if (partition_read_result.is_error()) {
      return partition_read_result.take_error_result();
    }

    if (partition.descriptor.magic != fvm::kPartitionDescriptorMagic) {
      return fpromise::error(
          "Fvm Sparse Image Partition descriptor contains incorrect magic. Expected " +
          std::to_string(fvm::kPartitionDescriptorMagic) + ", but found " +
          std::to_string(partition.descriptor.magic) + ".");
    }

    if ((partition.descriptor.flags & ~fvm::kSparseFlagAllValid) != 0) {
      return fpromise::error("Fvm Sparse Image Partition descriptor contains unknown flags.");
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
        return fpromise::error("Fvm Sparse Image Partition " + std::to_string(i) +
                               " extent descriptor " + std::to_string(j) +
                               " contains invalid magic. Expected " +
                               std::to_string(fvm::kExtentDescriptorMagic) + ", but found " +
                               std::to_string(extent.magic) + ".");
      }

      if (extent.extent_length > extent.slice_count * header.slice_size) {
        return fpromise::error("Fvm Sparse Image Partition " + std::to_string(i) +
                               " extent descriptor " + std::to_string(j) + " extent length(" +
                               std::to_string(extent.extent_length) +
                               ") exceeds the allocated slice range(" +
                               std::to_string(extent.slice_count * header.slice_size) + "), " +
                               std::to_string(extent.slice_count) + " allocated slices of size " +
                               std::to_string(header.slice_size) + ".");
      }

      if (!AddRange(allocated_ranges, extent.slice_start, extent.slice_count)) {
        return fpromise::error("Fvm Sparse Image Partition " + std::to_string(i) +
                               " extent descriptor " + std::to_string(j) +
                               " contains overlapping slice ranges.");
      }

      current_offset += sizeof(fvm::ExtentDescriptor);
      partition.extents.push_back(extent);
    }
  }

  return fpromise::ok(partitions);
}

CompressionOptions GetCompressionOptions(const fvm::SparseImage& header) {
  CompressionOptions options;
  options.schema = CompressionSchema::kNone;
  if ((header.flags & fvm::kSparseFlagLz4) != 0) {
    options.schema = CompressionSchema::kLz4;
  }

  return options;
}

fpromise::result<fvm::Header, std::string> ConvertToFvmHeader(
    const fvm::SparseImage& sparse_header, uint64_t slice_count,
    const std::optional<FvmOptions>& options) {
  // Generate the appropiate FVM header.

  std::optional<uint64_t> max_volume_size;
  std::optional<uint64_t> target_volume_size;

  if (sparse_header.maximum_disk_size > 0) {
    max_volume_size = sparse_header.maximum_disk_size;
  }

  if (options.has_value()) {
    if (options->max_volume_size.has_value()) {
      max_volume_size = options->max_volume_size;
    }
    if (options->target_volume_size.has_value()) {
      target_volume_size = options->target_volume_size;
    }
  }

  fvm::Header header =
      fvm::Header::FromSliceCount(fvm::kMaxUsablePartitions, slice_count, sparse_header.slice_size);

  // Fit to the provided slices.
  if (!target_volume_size.has_value() && !max_volume_size.has_value()) {
    return fpromise::ok(header);
  }
  if (max_volume_size.has_value() && max_volume_size.value() > 0) {
    if (max_volume_size.value() < header.fvm_partition_size) {
      return fpromise::error("|max_volume_size|(" + std::to_string(max_volume_size.value()) +
                             ") is smaller than the required space(" +
                             std::to_string(header.fvm_partition_size) + ") for " +
                             std::to_string(slice_count) + " slices of size(" +
                             std::to_string(sparse_header.slice_size) + ").");
    }
    header = fvm::Header::FromGrowableDiskSize(
        fvm::kMaxUsablePartitions, target_volume_size.value_or(header.fvm_partition_size),
        max_volume_size.value(), sparse_header.slice_size);

    // When the metadata is big enough, there wont be space for the slices, this will update the
    // minimum partition size to match that of a minimum number of slices, when there is no targeted
    // volume size.
    if (header.pslice_count == 0 && !target_volume_size.has_value()) {
      header.SetSliceCount(slice_count);
    }
  } else {
    header = fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions,
                                       target_volume_size.value_or(header.fvm_partition_size),
                                       sparse_header.slice_size);
  }
  if (slice_count > header.GetAllocationTableUsedEntryCount()) {
    return fpromise::error(
        "Fvm Sparse Image Reader found " + std::to_string(slice_count) +
        " slices, but |max_volume_size|(" + std::to_string(max_volume_size.value_or(0)) +
        ") with expected volume size(" + std::to_string(header.fvm_partition_size) + ") allows " +
        std::to_string(header.GetAllocationTableUsedEntryCount()) + " slices");
  }

  return fpromise::ok(header);
}

fpromise::result<fvm::Metadata, std::string> ConvertToFvmMetadata(
    const fvm::Header& header, cpp20::span<const PartitionEntry> partition_entries) {
  std::vector<fvm::VPartitionEntry> vpartition_entries;
  std::vector<fvm::SliceEntry> slice_entries;

  vpartition_entries.reserve(partition_entries.size());
  uint64_t current_vpartition = 0;
  for (const auto& partition_entry : partition_entries) {
    uint64_t slice_count = 0;

    for (const auto& extent_entry : partition_entry.extents) {
      for (uint64_t i = 0; i < extent_entry.slice_count; ++i) {
        fvm::SliceEntry entry = {};
        entry.Set(current_vpartition + 1, extent_entry.slice_start + i);
        slice_entries.push_back(entry);
      }
      slice_count += extent_entry.slice_count;
    }

    fvm::VPartitionEntry vpartition_entry = {};
    memcpy(vpartition_entry.unsafe_name, partition_entry.descriptor.name,
           sizeof(fvm::VPartitionEntry::unsafe_name));
    memcpy(vpartition_entry.type, partition_entry.descriptor.type,
           sizeof(fvm::VPartitionEntry::type));
    memcpy(vpartition_entry.guid, fvm::kPlaceHolderInstanceGuid.data(),
           sizeof(fvm::VPartitionEntry::type));
    // Currently non of the sparse partition flags propagate anything to VPartition::flags.
    // TODO(gevalentino): hide this behind an API, so we can have a single point of translation.
    vpartition_entry.flags = 0;
    vpartition_entry.slices = slice_count;
    vpartition_entries.push_back(vpartition_entry);
    current_vpartition++;
  }

  auto metadata_or =
      fvm::Metadata::Synthesize(header, vpartition_entries.data(), vpartition_entries.size(),
                                slice_entries.data(), slice_entries.size());
  if (metadata_or.is_error()) {
    return fpromise::error("Failed to synthesize metadata. Returned code : " +
                           std::to_string(metadata_or.error_value()));
  }
  return fpromise::ok(std::move(metadata_or.value()));
}

}  // namespace fvm_sparse_internal

fpromise::result<uint64_t, std::string> FvmSparseWriteImage(const FvmDescriptor& descriptor,
                                                            Writer* writer,
                                                            Compressor* compressor) {
  if (compressor == nullptr) {
    NoopCompressor noop_compressor;
    return FvmSparseWriteImageInternal(descriptor, writer, &noop_compressor);
  }
  return FvmSparseWriteImageInternal(descriptor, writer, compressor);
}

fpromise::result<bool, std::string> FvmSparseDecompressImage(uint64_t offset, const Reader& reader,
                                                             Writer& writer) {
  auto header_or = fvm_sparse_internal::GetHeader(offset, reader);
  if (header_or.is_error()) {
    return header_or.take_error_result();
  }

  // Check that everything looks good metadata wise, that is that partition and extent descriptors
  // are well formed, so we can abort early on any error. The entries themselves are unimportant for
  // decompressing the image.
  auto partition_entries_or =
      fvm_sparse_internal::GetPartitions(sizeof(fvm::SparseImage), reader, header_or.value());
  if (partition_entries_or.is_error()) {
    return partition_entries_or.take_error_result();
  }

  auto compression_options = fvm_sparse_internal::GetCompressionOptions(header_or.value());
  if (compression_options.schema == CompressionSchema::kNone) {
    return fpromise::ok(false);
  }

  uint64_t accumulated_offset = 0;
  // Copy the header and partition info first.
  std::vector<uint8_t> metadata_buffer;
  metadata_buffer.resize(header_or.value().header_length, 0);

  auto metadata_read_result = reader.Read(0, metadata_buffer);
  if (metadata_read_result.is_error()) {
    return metadata_read_result.take_error_result();
  }
  // Remove the compression flag.
  header_or.value().flags ^= fvm::kSparseFlagLz4;
  memcpy(metadata_buffer.data(), &header_or.value(), sizeof(fvm::SparseImage));

  auto metadata_write_result = writer.Write(0, metadata_buffer);
  if (metadata_write_result.is_error()) {
    return metadata_write_result.take_error_result();
  }
  accumulated_offset += header_or.value().header_length;

  auto decompressor_or = Lz4Decompressor::Create(compression_options);
  if (decompressor_or.is_error()) {
    return decompressor_or.take_error_result();
  }
  auto decompressor = decompressor_or.take_value();

  auto write_decompressed =
      [&accumulated_offset, &writer](
          cpp20::span<const uint8_t> decompressed_data) -> fpromise::result<void, std::string> {
    auto write_result = writer.Write(accumulated_offset, decompressed_data);
    if (write_result.is_error()) {
      return write_result;
    }
    accumulated_offset += decompressed_data.size();
    return fpromise::ok();
  };

  auto prepare_or = decompressor.Prepare(write_decompressed);
  if (prepare_or.is_error()) {
    return prepare_or.take_error_result();
  }

  std::vector<uint8_t> compressed_data;
  constexpr uint64_t kMaxBufferSize = (64 << 10);
  compressed_data.resize(std::min(kMaxBufferSize, reader.length()), 0);

  uint64_t read_offset = header_or.value().header_length;
  uint64_t last_hint = reader.length();
  while (read_offset < reader.length()) {
    auto compressed_view = cpp20::span<uint8_t>(compressed_data);

    if (compressed_view.size() > reader.length() - read_offset) {
      compressed_view = compressed_view.subspan(0, reader.length() - read_offset);
    }

    if (last_hint < compressed_view.size()) {
      compressed_view = compressed_view.subspan(0, last_hint);
    }

    decompressor.ProvideSizeHint(compressed_view.size());

    auto read_or = reader.Read(read_offset, compressed_view);
    if (read_or.is_error()) {
      return read_or.take_error_result();
    }

    auto decompressed_or = decompressor.Decompress(compressed_view);
    if (decompressed_or.is_error()) {
      return decompressed_or.take_error_result();
    }
    auto [hint, read_bytes] = decompressed_or.value();

    // Decompression finished.
    if (hint == 0) {
      auto finalize_result = decompressor.Finalize();
      if (finalize_result.is_error()) {
        return finalize_result.take_error_result();
      }
      break;
    }

    read_offset += read_bytes;
    if (hint > compressed_data.size()) {
      compressed_data.resize(hint, 0);
    }
    last_hint = hint;
  }

  return fpromise::ok(true);
}

fpromise::result<FvmDescriptor, std::string> FvmSparseReadImage(uint64_t offset,
                                                                std::unique_ptr<Reader> reader) {
  if (!reader) {
    return fpromise::error("Invalid |reader| for reading sparse image.");
  }

  std::shared_ptr<Reader> image_reader(reader.release());

  auto header_or = fvm_sparse_internal::GetHeader(offset, *image_reader);
  if (header_or.is_error()) {
    return header_or.take_error_result();
  }
  auto header = header_or.take_value();

  // Get the partition entries.
  auto partition_entries_or =
      fvm_sparse_internal::GetPartitions(sizeof(fvm::SparseImage), *image_reader, header);
  if (partition_entries_or.is_error()) {
    return partition_entries_or.take_error_result();
  }

  // This is the maximum offset allowed for the sparse image.
  uint64_t total_image_size = header.header_length;
  for (const auto& partition_entry : partition_entries_or.value()) {
    for (const auto& extent : partition_entry.extents) {
      total_image_size += extent.extent_length;
    }
  }

  // Get the matching options.
  FvmOptions options;
  options.slice_size = header.slice_size;

  if (header.maximum_disk_size != 0) {
    options.max_volume_size = header.maximum_disk_size;
  }

  FvmDescriptor::Builder builder;
  builder.SetOptions(options);

  // Generate the address map for each partition entry.
  uint64_t image_extent_offset = header.header_length;
  for (auto& partition_entry : partition_entries_or.value()) {
    VolumeDescriptor volume_descriptor;
    AddressDescriptor address_descriptor;

    volume_descriptor.encryption = (partition_entry.descriptor.flags & fvm::kSparseFlagZxcrypt) != 0
                                       ? EncryptionType::kZxcrypt
                                       : EncryptionType::kNone;
    std::string_view name(reinterpret_cast<const char*>(partition_entry.descriptor.name),
                          sizeof(fvm::PartitionDescriptor::name));
    name = name.substr(0, name.find('\0'));
    volume_descriptor.name = name;

    memcpy(volume_descriptor.instance.data(), fvm::kPlaceHolderInstanceGuid.data(),
           sizeof(VolumeDescriptor::type));
    memcpy(volume_descriptor.type.data(), partition_entry.descriptor.type,
           sizeof(VolumeDescriptor::type));

    if ((partition_entry.descriptor.flags & fvm::kSparseFlagCorrupted) != 0) {
      volume_descriptor.options.insert(Option::kEmpty);
    }

    uint64_t accumulated_extent_offset = 0;
    for (const auto& extent : partition_entry.extents) {
      AddressMap mapping;
      mapping.count = extent.extent_length;
      mapping.source = accumulated_extent_offset;
      mapping.target = extent.slice_start * options.slice_size;
      mapping.size = extent.slice_count * options.slice_size;

      if ((header.flags & fvm::kSparseFlagZeroFillNotRequired) == 0) {
        mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;
      }
      address_descriptor.mappings.push_back(mapping);
      accumulated_extent_offset += extent.extent_length;
    }

    // If the image is compressed wrap it with a Lz4DecompressReader.
    std::shared_ptr<Reader> base_reader = image_reader;
    if (fvm_sparse_internal::GetCompressionOptions(header).schema == CompressionSchema::kLz4) {
      auto decompress_reader = std::make_shared<Lz4DecompressReader>(
          header.header_length, total_image_size, image_reader);
      if (auto result = decompress_reader->Initialize(); result.is_error()) {
        return result.take_error_result();
      }
      base_reader = decompress_reader;
    }

    std::unique_ptr<SharedReader> partition_reader =
        std::make_unique<SharedReader>(image_extent_offset, accumulated_extent_offset, base_reader);

    image_extent_offset += accumulated_extent_offset;

    builder.AddPartition(
        Partition(volume_descriptor, address_descriptor, std::move(partition_reader)));
  }

  return builder.Build();
}

}  // namespace storage::volume_image
