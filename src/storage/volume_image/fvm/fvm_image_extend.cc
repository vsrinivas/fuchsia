// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_image_extend.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/volume_image/fvm/fvm_metadata.h"

namespace storage::volume_image {

fpromise::result<uint64_t, std::string> FvmImageGetSize(const Reader& source_image) {
  auto metadata_or = FvmGetMetadata(source_image);
  if (metadata_or.is_error()) {
    return metadata_or.take_error_result();
  }
  const auto& header = metadata_or.value().GetHeader();
  return fpromise::ok(header.fvm_partition_size);
}

fpromise::result<void, std::string> FvmImageExtend(const Reader& source_image,
                                                   const FvmOptions& options,
                                                   Writer& target_image) {
  auto metadata_or = FvmGetMetadata(source_image);
  if (metadata_or.is_error()) {
    return metadata_or.take_error_result();
  }
  const auto& header = metadata_or.value().GetHeader();

  // At this point we know we have a valid header and metadata, so we can check the validity of the
  // options.

  // Calculate the minimum size for the metadata if target disk size is set.
  if (!options.target_volume_size.has_value()) {
    return fpromise::error("Must provide a target size to extend to.");
  }

  if (options.target_volume_size.value() < header.fvm_partition_size) {
    return fpromise::error("Cannot extend a source image to a smaller image size.");
  }

  // If someone chose to do the extend 'in-place' then, which is the usual case, we need to be
  // careful in the order in which we do the operations. First move all slices(if necessary) to
  // match the new offset. Starting from the last slice to the first one, so there is no data
  // overwritten.
  auto new_header = fvm::Header::FromDiskSize(
      header.GetPartitionTableEntryCount(), options.target_volume_size.value(), header.slice_size);

  // At most we read 64 Kb at a time, or one slice slice, whichever is smaller.
  // If updating this value, make sure that big slice test, uses a slice bigger than this.
  constexpr uint64_t kMaxBufferSize = 64u << 10;
  std::vector<uint8_t> read_buffer;
  read_buffer.resize(std::min(header.slice_size, kMaxBufferSize));

  for (uint64_t pslice = header.pslice_count; pslice >= 1; --pslice) {
    auto& slice_entry = metadata_or.value().GetSliceEntry(pslice);

    // If the slice is not allocated to any partition, dont bother.
    if (!slice_entry.IsAllocated()) {
      continue;
    }

    uint64_t read_slice_start = metadata_or.value().GetHeader().GetSliceDataOffset(pslice);
    uint64_t write_slice_start = new_header.GetSliceDataOffset(pslice);
    uint64_t moved_bytes = 0;

    // The size of the slice is arbitrary, so we se a maximum buffer size, and stream the contents.
    while (moved_bytes < metadata_or.value().GetHeader().slice_size) {
      auto chunk_view =
          cpp20::span<uint8_t>(read_buffer)
              .subspan(0, std::min(kMaxBufferSize, metadata_or.value().GetHeader().slice_size));

      if (auto read_result = source_image.Read(read_slice_start + moved_bytes, chunk_view);
          read_result.is_error()) {
        return read_result.take_error_result();
      }

      if (auto write_result = target_image.Write(write_slice_start + moved_bytes, chunk_view);
          write_result.is_error()) {
        return write_result.take_error_result();
      }
      moved_bytes += chunk_view.size();
    }
  }

  // Now we copy the new metadata over, which is the old metadata, plus new entries, this can be
  // done by recreating the metadata.
  auto new_metadata = metadata_or.value().CopyWithNewDimensions(new_header);
  if (new_metadata.is_error()) {
    return fpromise::error("Failed to synthesize metadata for extended FVM. Error code: " +
                           std::to_string(new_metadata.error_value()));
  }

  // Now write both copies into the new place.
  if (auto write_primary_metadata_result = target_image.Write(
          new_header.GetSuperblockOffset(fvm::SuperblockType::kPrimary),
          cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(new_metadata->Get()->data()),
                                     new_metadata->Get()->size()));
      write_primary_metadata_result.is_error()) {
    return write_primary_metadata_result.take_error_result();
  }

  if (auto write_secondary_metadata_result = target_image.Write(
          new_header.GetSuperblockOffset(fvm::SuperblockType::kSecondary),
          cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(new_metadata->Get()->data()),
                                     new_metadata->Get()->size()));
      write_secondary_metadata_result.is_error()) {
    return write_secondary_metadata_result.take_error_result();
  }

  return fpromise::ok();
}

fpromise::result<uint64_t, std::string> FvmImageGetTrimmedSize(const Reader& source_image) {
  auto metadata_or = FvmGetMetadata(source_image);
  if (metadata_or.is_error()) {
    return metadata_or.take_error_result();
  }

  const auto& header = metadata_or.value().GetHeader();

  std::optional<uint64_t> last_allocated_slice;
  for (uint64_t pslice = header.pslice_count; pslice > 0; --pslice) {
    const auto& slice_entry = metadata_or.value().GetSliceEntry(pslice);

    if (slice_entry.IsAllocated()) {
      last_allocated_slice = pslice;
      break;
    }
  }

  uint64_t last_offset =
      last_allocated_slice.has_value()
          ? header.GetSliceDataOffset(last_allocated_slice.value()) + header.slice_size
          : header.GetSliceDataOffset(1);

  if (last_offset < header.GetSuperblockOffset(fvm::SuperblockType::kPrimary)) {
    last_offset = header.GetSuperblockOffset(fvm::SuperblockType::kPrimary) +
                  header.GetMetadataAllocatedBytes();
  }

  if (last_offset < header.GetSuperblockOffset(fvm::SuperblockType::kSecondary)) {
    last_offset = header.GetSuperblockOffset(fvm::SuperblockType::kSecondary) +
                  header.GetMetadataAllocatedBytes();
  }

  return fpromise::ok(last_offset);
}

}  // namespace storage::volume_image
