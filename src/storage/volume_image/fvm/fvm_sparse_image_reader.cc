// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_sparse_image_reader.h"

#include <lib/fit/result.h>
#include <zircon/status.h>

#include <digest/digest.h>
#include <fvm/fvm-sparse.h>
#include <fvm/fvm.h>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/utils/lz4_decompressor.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {

// Returns a byte view of a fixed size struct.
template <typename T>
fbl::Span<uint8_t> FixedSizeStructToSpan(T& typed_content) {
  return fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(&typed_content), sizeof(T));
}

// Returns a byte view of an array of structs.
template <typename T>
fbl::Span<const uint8_t> ContainerToSpan(const T& container) {
  return fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(container.data()),
                                  container.size() * sizeof(*container.data()));
}

class DecompressionHelper {
 public:
  DecompressionHelper(Reader& base_reader, uint64_t start_offset)
      : base_reader_(base_reader), compressed_offset_(start_offset) {
    ZX_ASSERT(decompressor_
                  .Prepare([this](fbl::Span<const uint8_t> decompressed_data) {
                    decompressed_buffer_.insert(decompressed_buffer_.end(),
                                                decompressed_data.begin(), decompressed_data.end());
                    return fit::ok();
                  })
                  .is_ok());
  }

  fit::result<void, std::string> Read(uint64_t offset, fbl::Span<uint8_t> buffer) {
    size_t some;
    for (size_t done = 0; done < buffer.size(); done += some, offset += some) {
      bool making_progress = true;
      while (decompressed_buffer_.size() < std::min(kBufferSize, buffer.size() - done)) {
        if (!making_progress) {
          return fit::error("no progress with decompressor");
        }
        making_progress = false;
        if (compressed_buffer_.size() < kBufferSize &&
            compressed_offset_ < base_reader_.GetMaximumOffset()) {
          // Fill the compressed buffer.
          size_t current_size = compressed_buffer_.size();
          size_t len = static_cast<size_t>(std::min<uint64_t>(
              kBufferSize - current_size, base_reader_.GetMaximumOffset() - compressed_offset_));
          compressed_buffer_.resize(current_size + len);
          auto result = base_reader_.Read(
              compressed_offset_, fbl::Span<uint8_t>(&compressed_buffer_[current_size], len));
          if (result.is_error()) {
            return result.take_error_result();
          }
          compressed_offset_ += len;
        }
        if (!compressed_buffer_.empty()) {
          const size_t old_size = decompressed_buffer_.size();
          auto result = decompressor_.Decompress(fbl::Span<uint8_t>(compressed_buffer_));
          if (result.is_error()) {
            return result.take_error_result();
          }
          compressed_buffer_.erase(compressed_buffer_.begin(),
                                   compressed_buffer_.begin() + result.value().read_bytes);
          if (result.value().hint == 0) {
            decompressor_.Finalize();
            compressed_buffer_.clear();
          }
          if (result.value().read_bytes > 0 || decompressed_buffer_.size() > old_size) {
            making_progress = true;
          }
        }
      }
      if (offset != uncompressed_offset_) {
        // For now, all use cases that we have only require sequential reading.
        return fit::error("Non sequential reading is not supported");
      }
      // Copy from the decompression buffer into the caller's buffer.
      some = std::min(buffer.size() - done, decompressed_buffer_.size());
      memcpy(buffer.data() + done, decompressed_buffer_.data(), some);
      decompressed_buffer_.erase(decompressed_buffer_.begin(), decompressed_buffer_.begin() + some);
      uncompressed_offset_ += some;
    }
    return fit::ok();
  }

 private:
  static constexpr size_t kBufferSize = 1048576;

  const Reader& base_reader_;
  Lz4Decompressor decompressor_;
  // TODO: Consider using deques for this.
  std::vector<uint8_t> compressed_buffer_;
  std::vector<uint8_t> decompressed_buffer_;
  uint64_t compressed_offset_;
  uint64_t uncompressed_offset_ = 0;
};

class SparseImageReader : public Reader {
 public:
  // We synthesize the metadata at this offset.
  static constexpr uint64_t kMetadataOffset = 0x8000'0000'0000'0000ull;

  SparseImageReader(Reader& base_reader, uint64_t data_offset, std::vector<uint8_t> metadata)
      : decompression_helper_(base_reader, data_offset), metadata_(metadata) {}

  uint64_t GetMaximumOffset() const override { return kMetadataOffset + 2 * metadata_.size(); }

  fit::result<void, std::string> Read(uint64_t offset, fbl::Span<uint8_t> buffer) const override {
    if (offset >= kMetadataOffset) {
      size_t some;
      for (size_t done = 0; done < buffer.size(); done += some, offset += some) {
        size_t metadata_offset = static_cast<size_t>((offset - kMetadataOffset) % metadata_.size());
        some = std::min(metadata_.size() - metadata_offset, buffer.size() - done);
        memcpy(buffer.data() + done, metadata_.data() + metadata_offset, some);
      }
      return fit::ok();
    } else {
      return decompression_helper_.Read(offset, buffer);
    }
  }

 private:
  mutable DecompressionHelper decompression_helper_;
  std::vector<uint8_t> metadata_;
};

fit::result<Partition, std::string> OpenSparseImage(Reader& base_reader) {
  // Start by reading the header.
  fvm::SparseImage fvm_sparse_header;
  auto result = base_reader.Read(0, FixedSizeStructToSpan(fvm_sparse_header));
  if (result.is_error()) {
    return result.take_error_result();
  }

  if (fvm_sparse_header.magic != fvm::kSparseFormatMagic) {
    return fit::error("Unrecognized magic in sparse header");
  }
  if (fvm_sparse_header.version != fvm::kSparseFormatVersion) {
    return fit::error("Unsupported sparse version");
  }
  if ((fvm_sparse_header.flags & fvm::kSparseFlagLz4) == 0) {
    return fit::error("Only Lz4 supported");
  }

  const uint64_t slice_size = fvm_sparse_header.slice_size;

  // Read all the extents
  std::vector<fvm::VPartitionEntry> fvm_partitions(1);  // First entry is kept empty.
  std::vector<fvm::SliceEntry> slices(1);               // First entry is kept empty.
  using Extent = std::pair<fvm::ExtentDescriptor, uint64_t>;
  std::vector<Extent> extents;
  uint64_t offset = sizeof(fvm_sparse_header);  // Current offset in the source file.
  uint64_t data_offset = 0;                     // Current data offset in uncompressed space.
  uint64_t total_slices = 0;

  // For all partitions...
  for (uint64_t partition_index = 0; partition_index < fvm_sparse_header.partition_count;
       ++partition_index) {
    fvm::PartitionDescriptor partition_descriptor;
    auto result = base_reader.Read(offset, FixedSizeStructToSpan(partition_descriptor));
    if (result.is_error()) {
      return result.take_error_result();
    }
    offset += sizeof(partition_descriptor);

    static uint8_t empty_guid[fvm::kGuidSize];
    int allocated_slices = 0;

    // For all extents within the partition...
    for (uint32_t i = 0; i < partition_descriptor.extent_count; ++i) {
      fvm::ExtentDescriptor extent;
      auto result = base_reader.Read(offset, FixedSizeStructToSpan(extent));
      if (result.is_error()) {
        return result.take_error_result();
      }
      offset += sizeof(extent);
      extents.push_back(std::make_pair(extent, data_offset));
      data_offset += extent.extent_length;

      // Push FVM's allocation metadata
      for (uint64_t slice = extent.slice_start; slice < extent.slice_start + extent.slice_count;
           ++slice) {
        slices.push_back(fvm::SliceEntry::Create(partition_index + 1, slice));
      }
      allocated_slices += extent.slice_count;
    }

    // Push FVM's partition entry
    fvm_partitions.push_back(fvm::VPartitionEntry::Create(
        partition_descriptor.type,
        empty_guid,  // TODO: fvm needs to initialise this
        allocated_slices, fvm::VPartitionEntry::Name(partition_descriptor.name),
        0));  // TODO: figure out flags

    total_slices += allocated_slices;
  }

  // Remember the first offset where data starts.
  const uint64_t data_start = offset;
  if (base_reader.GetMaximumOffset() <= data_start) {
    return fit::error("bad maximum offset from base reader");
  }

  uint64_t disk_size = fvm_sparse_header.maximum_disk_size;
  if (disk_size == 0) {
    // If the sparse image includes a maximum disk size, use that, but otherwise, compute the disk
    // size using the number of allocated slices.  This will only allow limited growth (i.e. FVM's
    // metadata can only grow to a block boundary).
    const uint64_t metadata_size =
        fvm::AllocationTable::kOffset +
        fbl::round_up(sizeof(fvm::SliceEntry) * total_slices, fvm::kBlockSize);
    disk_size = metadata_size * 2 + total_slices * slice_size;
  }

  fvm::FormatInfo format_info = fvm::FormatInfo::FromDiskSize(disk_size, slice_size);
  ZX_ASSERT(total_slices <= format_info.slice_count());

  // Now we need to synthesize the FVM metadata, which consists of the super-block a.k.a
  // fvm::Header, partition table, followed by the allocation table.
  std::vector<uint8_t> metadata;
  auto append_metadata = [&metadata](fbl::Span<const uint8_t> data) {
    metadata.insert(metadata.end(), data.begin(), data.end());
  };

  fvm::Header header{
      .magic = fvm::kMagic,
      .version = fvm::kVersion,
      .pslice_count = format_info.slice_count(),
      .slice_size = fvm_sparse_header.slice_size,
      .fvm_partition_size = disk_size,
      .vpartition_table_size = fvm::PartitionTable::kLength,
      .allocation_table_size = format_info.GetMaxAllocatableSlices() * sizeof(fvm::SliceEntry),
  };

  append_metadata(FixedSizeStructToSpan(header));

  metadata.resize(fvm::PartitionTable::kOffset);
  append_metadata(ContainerToSpan(fvm_partitions));

  metadata.resize(fvm::AllocationTable::kOffset);
  append_metadata(ContainerToSpan(slices));

  metadata.resize(format_info.metadata_size());

  digest::Digest digest;
  fvm::Header* header_ptr = reinterpret_cast<fvm::Header*>(metadata.data());
  memcpy(header_ptr->hash, digest.Hash(metadata.data(), metadata.size()), sizeof(header_ptr->hash));

  zx_status_t status =
      fvm::ValidateHeader(metadata.data(), metadata.data(), metadata.size(), nullptr);
  if (status != ZX_OK) {
    return fit::error(std::string("Generated header is unexpectedly bad: ") +
#ifdef __Fuchsia__
                      zx_status_get_string(status));
#else
                      "error: " + std::to_string(status));
#endif
  }

  // Build the address mappings now.
  AddressDescriptor address_descriptor;

  // Push a single mapping for the metadata at a source offset we'll never use in the sparse image.
  address_descriptor.mappings.push_back(AddressMap{
      .source = SparseImageReader::kMetadataOffset,
      .target = 0,
      .count = metadata.size() * 2,
  });

  // Push the remaining mappings.
  uint64_t slice = 1;  // It's 1 indexed.
  for (const Extent& extent : extents) {
    AddressMap mapping = {.source = extent.second,
                          .target = format_info.GetSliceStart(slice),
                          .count = extent.first.extent_length,
                          .size = extent.first.slice_count * slice_size};
    mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;

    address_descriptor.mappings.push_back(std::move(mapping));
    slice += extent.first.slice_count;
  }

  // Now we can create a reader.
  auto reader = std::make_unique<SparseImageReader>(base_reader, data_start, std::move(metadata));
  VolumeDescriptor descriptor{.size = disk_size};
  return fit::ok(Partition(descriptor, address_descriptor, std::move(reader)));
}

}  // namespace storage::volume_image
