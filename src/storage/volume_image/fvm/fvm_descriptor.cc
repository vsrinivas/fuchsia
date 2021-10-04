// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_descriptor.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/block_utils.h"

namespace storage::volume_image {
namespace {

std::string ToSizeString(uint64_t bytes) {
  constexpr int kByteToMegabyte = 1 << 20;
  std::string size_str = std::to_string(static_cast<double>(bytes) / kByteToMegabyte);
  return size_str.append(" [MB]");
}

}  // namespace

namespace internal {

fvm::Header MakeHeader(const FvmOptions& options, uint64_t slice_count) {
  if (options.max_volume_size.has_value()) {
    return fvm::Header::FromGrowableDiskSize(
        fvm::kMaxUsablePartitions,
        options.target_volume_size.value_or(options.max_volume_size.value()),
        options.max_volume_size.value(), options.slice_size);
  }
  if (options.target_volume_size.has_value()) {
    return fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, options.target_volume_size.value(),
                                     options.slice_size);
  }
  return fvm::Header::FromSliceCount(fvm::kMaxUsablePartitions, slice_count, options.slice_size);
}

}  // namespace internal

FvmDescriptor::Builder::Builder(FvmDescriptor descriptor)
    : options_(std::move(descriptor.options_)),
      accumulated_slices_(descriptor.slice_count_),
      metadata_allocated_size_(descriptor.metadata_required_size_) {
  for (auto it = descriptor.partitions_.begin(); it != descriptor.partitions_.end();) {
    partitions_.emplace_back(std::move(descriptor.partitions_.extract(it++).value()));
  }
}

FvmDescriptor::Builder& FvmDescriptor::Builder::AddPartition(Partition partition) {
  partitions_.emplace_back(std::move(partition));
  return *this;
}

FvmDescriptor::Builder& FvmDescriptor::Builder::SetOptions(const FvmOptions& options) {
  options_ = options;
  return *this;
}

fpromise::result<FvmDescriptor, std::string> FvmDescriptor::Builder::Build() {
  FvmDescriptor descriptor;

  if (!options_.has_value()) {
    return fpromise::error("FVM Options were not set.");
  }

  if (options_->slice_size == 0) {
    return fpromise::error("FVM's slice_size must be greater than zero.");
  }

  if (options_->max_volume_size.has_value() &&
      options_->max_volume_size.value() < options_->target_volume_size.value_or(0)) {
    std::string error = "FVM's max_volume_size(";
    error.append(ToSizeString(options_->max_volume_size.value()))
        .append(") is smaller than target_volume_size(")
        .append(ToSizeString(options_->target_volume_size.value()))
        .append(").");
    return fpromise::error(error);
  }

  accumulated_slices_ = 0;
  // Check for duplicated partition entries <Name, InstanceGUID> unique pair.
  for (auto& partition : partitions_) {
    auto it = descriptor.partitions_.find(partition);
    if (it != descriptor.partitions_.end()) {
      std::string error = "Partition already exists, could not add partition " +
                          partition.volume().name + " and instance guid " +
                          Guid::ToString(partition.volume().instance).value() +
                          " failed.\n Partition" + it->volume().name + " and instance guid " +
                          Guid::ToString(it->volume().instance).value() + " was added before.";
      partitions_.clear();
      return fpromise::error(error);
    }

    // Update accumulated slice count, and check for overlapping extents.
    std::set<const AddressMap*, std::function<bool(const AddressMap*, const AddressMap*)>> extents(
        [](auto* a, auto* b) { return a->target < b->target; });
    for (const auto& mapping : partition.address().mappings) {
      for (auto it = extents.lower_bound(&mapping); it != extents.end(); ++it) {
        auto* current_extent = *it;
        // We are past the end of the extent.
        if (current_extent->target >= mapping.target + mapping.count) {
          break;
        }

        if (current_extent->target + current_extent->count > mapping.target) {
          // Get the other mapping
          return fpromise::error("Address descriptor of " + partition.volume().name +
                                 " contains overlapping mappings. Conflict between " +
                                 mapping.DebugString() + " and " + current_extent->DebugString());
        }
      }
      extents.insert(&mapping);
      uint64_t required_size = mapping.size.value_or(mapping.count);
      accumulated_slices_ += GetBlockCount(mapping.target, required_size, options_->slice_size);
    }

    descriptor.partitions_.emplace(std::move(partition));
  }
  partitions_.clear();

  fvm::Header header = internal::MakeHeader(*options_, accumulated_slices_);
  metadata_allocated_size_ = 2 * header.GetMetadataAllocatedBytes();

  uint64_t minimum_size = metadata_allocated_size_ + accumulated_slices_ * options_->slice_size;
  // We are not allowed to exceed the  target disk size when set.
  if (minimum_size > options_->target_volume_size.value_or(std::numeric_limits<uint64_t>::max())) {
    std::string error =
        "Failed to build FVMDescriptor. Image does not fit in target volume size. Minimum size "
        "is " +
        ToSizeString(minimum_size) + " and target size is " +
        ToSizeString(options_->target_volume_size.value()) + ".";
    return fpromise::error(error);
  }

  descriptor.metadata_required_size_ = metadata_allocated_size_;
  descriptor.slice_count_ = accumulated_slices_;
  descriptor.options_ = std::move(options_.value());

  return fpromise::ok(std::move(descriptor));
}

fpromise::result<void, std::string> FvmDescriptor::WriteBlockImage(Writer& writer) const {
  fvm::Header header = internal::MakeHeader(options_, slice_count_);

  std::vector<fvm::VPartitionEntry> vpartitions;
  vpartitions.reserve(partitions_.size());

  std::vector<fvm::SliceEntry> slices;
  slices.reserve(slice_count_);

  // Partitions start at index 1.
  size_t current_vpartition = 1;
  for (const auto& partition : partitions()) {
    fvm::VPartitionEntry vpartition = {};
    uint64_t partition_slices = 0;

    memcpy(vpartition.unsafe_name, partition.volume().name.data(), partition.volume().name.size());
    memcpy(vpartition.type, partition.volume().type.data(), partition.volume().type.size());
    memcpy(vpartition.guid, partition.volume().instance.data(), partition.volume().instance.size());
    vpartition.flags = 0;

    for (const auto& mapping : partition.address().mappings) {
      // Slice info for each mapping.
      uint64_t size = std::max(mapping.count, mapping.size.value_or(0));
      uint64_t slice_count = GetBlockCount(mapping.target, size, options_.slice_size);
      partition_slices += slice_count;
      uint64_t start_slice = GetBlockFromBytes(mapping.target, options_.slice_size);

      if (!IsOffsetBlockAligned(mapping.target, options_.slice_size)) {
        return fpromise::error("Partition " + partition.volume().name +
                               " contains unaligned mapping " + std::to_string(mapping.target) +
                               ". FVM Sparse Image requires slice aligned extent |vslice_start|.");
      }

      // Slice entry for each slice in the mapping.
      for (uint64_t vslice_offset = 0; vslice_offset < slice_count; ++vslice_offset) {
        slices.emplace_back(current_vpartition, start_slice + vslice_offset);
      }
    }
    vpartition.slices = partition_slices;
    vpartitions.push_back(vpartition);
    current_vpartition++;
  }

  // At this point we've written all the slice contents, now write the metadata.
  auto fvm_metadata_or = fvm::Metadata::Synthesize(header, vpartitions.data(), vpartitions.size(),
                                                   slices.data(), slices.size());
  if (fvm_metadata_or.is_error()) {
    return fpromise::error(
        "FvmDescriptor::WriteBlockImage failed to synthesize fvm metadata with error code : " +
        std::to_string(fvm_metadata_or.status_value()));
  }
  auto fvm_metadata = std::move(fvm_metadata_or.value());

  auto metadata_view = cpp20::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(fvm_metadata.Get()->data()), fvm_metadata.Get()->size());
  auto metadata_write_result = writer.Write(
      fvm_metadata.GetHeader().GetSuperblockOffset(fvm::SuperblockType::kPrimary), metadata_view);
  if (metadata_write_result.is_error()) {
    return metadata_write_result.take_error_result();
  }

  auto secondary_metadata_write_result = writer.Write(
      fvm_metadata.GetHeader().GetSuperblockOffset(fvm::SuperblockType::kSecondary), metadata_view);
  if (secondary_metadata_write_result.is_error()) {
    return secondary_metadata_write_result.take_error_result();
  }

  // Now write the data for each slice starting at physical slice 1, since it 1-indexed.
  // This is achieved by streaming the data into the slices, in the same order they are read.
  //
  // Slices that are prefilled will have 0, and slices that are allocated but not used will be
  // skipped.
  //
  // In order to guarantee that image has the right size, if the last slice of the image is not
  // written, the last block of the last slice, will be filled with zeroes. This will force the
  // image to have the right size, if its growing dynamically.
  uint64_t current_physical_slice = 1;
  std::vector<uint8_t> slice_buffer;
  slice_buffer.resize(options_.slice_size, 0);

  for (const auto& partition : partitions()) {
    for (const auto& mapping : partition.address().mappings) {
      uint64_t size = std::max(mapping.count, mapping.size.value_or(0));
      uint64_t slice_count = GetBlockCount(mapping.target, size, options_.slice_size);
      uint64_t data_slice_count = GetBlockCount(mapping.target, mapping.count, options_.slice_size);

      // Check if we should fill non data backed slices in this partition mapping explicitly.
      auto fill_value_it = mapping.options.find(EnumAsString(AddressMapOption::kFill));
      std::optional<uint8_t> fill_value = std::nullopt;
      if (fill_value_it != mapping.options.end()) {
        fill_value = static_cast<uint8_t>(fill_value_it->second);
      }
      for (uint64_t slice = 0; slice < slice_count; ++slice) {
        auto slice_data_view = cpp20::span<uint8_t>(slice_buffer);
        if (slice < data_slice_count) {
          // Byte offset of current slice.
          const uint64_t slice_offset = slice * options_.slice_size;
          const uint64_t vslice_offset =
              volume_image::GetBlockFromBytes(mapping.target, options_.slice_size) *
                  options_.slice_size +
              slice_offset;

          // Check if the start of the extent is not aligned with the slice.
          // All extents targets are slice aligned, but that may not be the case when reading
          // from the source.
          const uint64_t data_vslice_start =
              mapping.target > vslice_offset ? mapping.target - vslice_offset : 0;

          // Check if the extent data does not end in slice boundary.
          const uint64_t data_vslice_end = mapping.target + mapping.count - vslice_offset;
          const uint64_t vslice_end = vslice_offset + options_.slice_size - vslice_offset;
          uint64_t data_length = data_vslice_end < vslice_end ? data_vslice_end - data_vslice_start
                                                              : vslice_end - data_vslice_start;
          slice_data_view = slice_data_view.subspan(data_vslice_start, data_length);
          auto read_result = partition.reader()->Read(
              mapping.source + slice_offset + data_vslice_start, slice_data_view);
          if (read_result.is_error()) {
            return read_result.take_error_result();
          }
        }

        // Skip all allocated slices that are not backed by data if we dont need to fill.
        if (slice >= data_slice_count && !fill_value.has_value()) {
          current_physical_slice += slice_count - data_slice_count;
          break;
        }

        // Finally write current slice.
        const uint64_t physical_slice_offset = header.GetSliceDataOffset(current_physical_slice);
        auto write_result = writer.Write(physical_slice_offset, slice_buffer);
        if (write_result.is_error()) {
          return write_result.take_error_result();
        }

        // Clean up the buffer for next read.
        memset(slice_buffer.data(), fill_value.value_or(0), slice_buffer.size());
        current_physical_slice++;
      }
    }
  }
  // Account for slice zero.
  ZX_ASSERT(slices.size() + 1 == current_physical_slice);
  return fpromise::ok();
}

}  // namespace storage::volume_image
