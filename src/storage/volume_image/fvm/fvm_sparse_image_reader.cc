// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_sparse_image_reader.h"

#include <lib/fpromise/result.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <string>

#include "src/lib/digest/digest.h"
#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_sparse.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/utils/lz4_decompressor.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {

namespace {

// Returns a byte view of a fixed size struct.
template <typename T>
cpp20::span<uint8_t> FixedSizeStructToSpan(T& typed_content) {
  return cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&typed_content), sizeof(T));
}

class DecompressionHelper {
 public:
  DecompressionHelper(Reader& base_reader, uint64_t start_offset)
      : base_reader_(base_reader), compressed_offset_(start_offset) {
    ZX_ASSERT(decompressor_
                  .Prepare([this](cpp20::span<const uint8_t> decompressed_data) {
                    decompressed_buffer_.insert(decompressed_buffer_.end(),
                                                decompressed_data.begin(), decompressed_data.end());
                    return fpromise::ok();
                  })
                  .is_ok());
  }

  fpromise::result<void, std::string> Read(uint64_t offset, cpp20::span<uint8_t> buffer) {
    size_t some;
    for (size_t done = 0; done < buffer.size(); done += some, offset += some) {
      bool making_progress = true;
      while (decompressed_buffer_.size() < std::min(kBufferSize, buffer.size() - done)) {
        if (!making_progress) {
          return fpromise::error("no progress with decompressor");
        }
        making_progress = false;
        if (compressed_buffer_.size() < kBufferSize && compressed_offset_ < base_reader_.length()) {
          // Fill the compressed buffer.
          size_t current_size = compressed_buffer_.size();
          size_t len = static_cast<size_t>(std::min<uint64_t>(
              kBufferSize - current_size, base_reader_.length() - compressed_offset_));
          compressed_buffer_.resize(current_size + len);
          auto result = base_reader_.Read(
              compressed_offset_, cpp20::span<uint8_t>(&compressed_buffer_[current_size], len));
          if (result.is_error()) {
            return result.take_error_result();
          }
          compressed_offset_ += len;
        }
        if (!compressed_buffer_.empty()) {
          const size_t old_size = decompressed_buffer_.size();
          auto result = decompressor_.Decompress(cpp20::span<uint8_t>(compressed_buffer_));
          if (result.is_error()) {
            return result.take_error_result();
          }
          compressed_buffer_.erase(
              compressed_buffer_.begin(),
              compressed_buffer_.begin() + static_cast<ptrdiff_t>(result.value().read_bytes));
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
        return fpromise::error("Non sequential reading is not supported");
      }
      // Copy from the decompression buffer into the caller's buffer.
      some = std::min(buffer.size() - done, decompressed_buffer_.size());
      memcpy(buffer.data() + done, decompressed_buffer_.data(), some);
      decompressed_buffer_.erase(decompressed_buffer_.begin(),
                                 decompressed_buffer_.begin() + static_cast<ptrdiff_t>(some));
      uncompressed_offset_ += some;
    }
    return fpromise::ok();
  }

 private:
  static constexpr size_t kBufferSize{static_cast<size_t>(64) * (1u << 10)};

  const Reader& base_reader_;
  Lz4Decompressor decompressor_ = Lz4Decompressor(kBufferSize);

  // TODO: Consider using deques for this. Or just keeping track of the decompressed length in a
  // fixed size buffer to avoid vector resizing operations.
  std::vector<uint8_t> compressed_buffer_;
  std::vector<uint8_t> decompressed_buffer_;
  uint64_t compressed_offset_;
  uint64_t uncompressed_offset_ = 0;
};

class SparseImageReader : public Reader {
 public:
  // We synthesize the metadata at this offset.
  static constexpr uint64_t kMetadataOffset = 0x8000'0000'0000'0000ull;
  static constexpr bool IsMetadata(uint64_t offset) { return offset >= kMetadataOffset; }

  SparseImageReader(Reader& base_reader, uint64_t data_offset, fvm::Metadata&& metadata)
      : decompression_helper_(base_reader, data_offset), metadata_(std::move(metadata)) {}

  uint64_t length() const override { return kMetadataOffset + metadata_.Get()->size(); }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const override {
    if (IsMetadata(offset)) {
      size_t some = 0;
      const fvm::MetadataBuffer* raw_metadata = metadata_.Get();
      for (size_t done = 0; done < buffer.size(); done += some, offset += some) {
        size_t metadata_offset =
            static_cast<size_t>((offset - kMetadataOffset) % raw_metadata->size());
        some = std::min(raw_metadata->size() - metadata_offset, buffer.size() - done);
        memcpy(buffer.data() + done,
               static_cast<const uint8_t*>(raw_metadata->data()) + metadata_offset, some);
      }
      return fpromise::ok();
    } else {
      return decompression_helper_.Read(offset, buffer);
    }
  }

 private:
  mutable DecompressionHelper decompression_helper_;
  fvm::Metadata metadata_;
};

}  // namespace

fpromise::result<Partition, std::string> OpenSparseImage(
    Reader& base_reader, std::optional<uint64_t> maximum_disk_size) {
  // Start by reading the header.
  fvm::SparseImage fvm_sparse_header;
  auto result = base_reader.Read(0, FixedSizeStructToSpan(fvm_sparse_header));
  if (result.is_error()) {
    return result.take_error_result();
  }

  if (fvm_sparse_header.magic != fvm::kSparseFormatMagic) {
    return fpromise::error("Unrecognized magic in sparse header");
  }
  if (fvm_sparse_header.version != fvm::kSparseFormatVersion) {
    return fpromise::error("Unsupported sparse version");
  }
  if ((fvm_sparse_header.flags & fvm::kSparseFlagLz4) == 0) {
    return fpromise::error("Only Lz4 supported");
  }

  if (maximum_disk_size.has_value()) {
    fvm_sparse_header.maximum_disk_size = maximum_disk_size.value();
  }

  const uint64_t slice_size = fvm_sparse_header.slice_size;

  // Read all the extents
  std::vector<fvm::VPartitionEntry> fvm_partitions;
  std::vector<fvm::SliceEntry> slices;
  using Extent = std::pair<fvm::ExtentDescriptor, uint64_t>;
  std::vector<Extent> extents;
  uint64_t offset = sizeof(fvm_sparse_header);  // Current offset in the source file.
  uint64_t data_offset = 0;                     // Current data offset in uncompressed space.

  // For all partitions...
  for (uint64_t partition_index = 0; partition_index < fvm_sparse_header.partition_count;
       ++partition_index) {
    fvm::PartitionDescriptor partition_descriptor;
    auto result = base_reader.Read(offset, FixedSizeStructToSpan(partition_descriptor));
    if (result.is_error()) {
      return result.take_error_result();
    }
    offset += sizeof(partition_descriptor);

    uint64_t allocated_slices = 0;

    // For all extents within the partition...
    for (uint32_t i = 0; i < partition_descriptor.extent_count; ++i) {
      fvm::ExtentDescriptor extent;
      auto result = base_reader.Read(offset, FixedSizeStructToSpan(extent));
      if (result.is_error()) {
        return result.take_error_result();
      }
      offset += sizeof(extent);
      extents.emplace_back(extent, data_offset);
      data_offset += extent.extent_length;

      // Push FVM's allocation metadata
      for (uint64_t slice = extent.slice_start; slice < extent.slice_start + extent.slice_count;
           ++slice) {
        // The +1 is because sparse images 0-index their partitions, but FVM 1-indexes.
        slices.emplace_back(partition_index + 1, slice);
      }
      allocated_slices += extent.slice_count;
    }

    const uint8_t* type = partition_descriptor.type;
    const uint8_t* guid = fvm::kPlaceHolderInstanceGuid.data();
    // Push FVM's partition entry
    fvm_partitions.emplace_back(type, guid, allocated_slices,
                                fvm::VPartitionEntry::StringFromArray(partition_descriptor.name));
  }

  // Remember the first offset where data starts.
  const uint64_t data_start = offset;
  if (base_reader.length() <= data_start) {
    return fpromise::error("bad maximum offset from base reader");
  }

  fvm::Header header;
  if (fvm_sparse_header.maximum_disk_size) {
    // The sparse image includes a maximum disk size, use that.
    header = fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions,
                                       fvm_sparse_header.maximum_disk_size, slice_size);
    if (slices.size() > header.GetAllocationTableUsedEntryCount()) {
      return fpromise::error("Fvm Sparse Image Reader, found " + std::to_string(slices.size()) +
                             ", but disk size allows " +
                             std::to_string(header.GetAllocationTableUsedEntryCount()) + ".");
    }
  } else {
    // When no disk size is specified, compute the disk size using the number of allocated slices.
    // This will allow limited growth (i.e. FVM's metadata can only grow to a block boundary).
    header = fvm::Header::FromSliceCount(fvm::kMaxUsablePartitions, slices.size(), slice_size);
  }
  zx::result<fvm::Metadata> metadata_or = fvm::Metadata::Synthesize(
      header, fvm_partitions.data(), fvm_partitions.size(), slices.data(), slices.size());
  if (metadata_or.is_error()) {
    return fpromise::error("Generating FVM metadata failed: " +
                           std::to_string(metadata_or.status_value()));
  }

  // Build the address mappings now.
  AddressDescriptor address_descriptor;

  // Push mappings for the metadata at source offsets we'll never use in the sparse image.
  // Both the A/B copies have the same source, pointing to the synthesized metadata.
  address_descriptor.mappings.push_back(AddressMap{
      .source = SparseImageReader::kMetadataOffset,
      .target = header.GetSuperblockOffset(fvm::SuperblockType::kPrimary),
      .count = metadata_or->Get()->size(),
  });
  address_descriptor.mappings.push_back(AddressMap{
      .source = SparseImageReader::kMetadataOffset,
      .target = header.GetSuperblockOffset(fvm::SuperblockType::kSecondary),
      .count = metadata_or->Get()->size(),
  });

  // Push the remaining mappings.
  uint64_t slice = 1;  // It's 1 indexed.
  for (const Extent& extent : extents) {
    AddressMap mapping = {.source = extent.second,
                          .target = header.GetSliceDataOffset(slice),
                          .count = extent.first.extent_length,
                          .size = extent.first.slice_count * slice_size};
    if (!(fvm_sparse_header.flags & fvm::kSparseFlagZeroFillNotRequired)) {
      mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;
    }

    address_descriptor.mappings.push_back(std::move(mapping));
    slice += extent.first.slice_count;
  }

  VolumeDescriptor descriptor{.size = header.fvm_partition_size};

  // Now we can create a reader.
  auto reader =
      std::make_unique<SparseImageReader>(base_reader, data_start, std::move(metadata_or.value()));
  return fpromise::ok(Partition(descriptor, address_descriptor, std::move(reader)));
}

}  // namespace storage::volume_image
