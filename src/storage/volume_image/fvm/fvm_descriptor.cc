// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_descriptor.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>
#include <fvm/format.h>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/block_utils.h"

namespace storage::volume_image {
namespace {

std::string ToSizeString(uint64_t bytes) {
  constexpr int kByteToMegabyte = 2 << 20;
  std::string size_str = std::to_string(static_cast<double>(bytes) / kByteToMegabyte);
  return size_str.append(" [MB]");
}

}  // namespace

namespace internal {

fvm::Header MakeHeader(const FvmOptions& options, uint64_t slice_count) {
  if (options.max_volume_size.has_value()) {
    return fvm::FormatInfo::FromDiskSize(options.max_volume_size.value(), options.slice_size)
        .header();
  }
  if (options.target_volume_size.has_value()) {
    return fvm::FormatInfo::FromDiskSize(options.target_volume_size.value(), options.slice_size)
        .header();
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

fit::result<FvmDescriptor, std::string> FvmDescriptor::Builder::Build() {
  FvmDescriptor descriptor;

  if (!options_.has_value()) {
    return fit::error("FVM Options were not set.");
  }

  if (options_->slice_size == 0) {
    return fit::error("FVM's slice_size must be greater than zero.");
  }

  if (options_->max_volume_size.has_value() &&
      options_->max_volume_size.value() < options_->target_volume_size.value_or(0)) {
    std::string error = "FVM's max_volume_size(";
    error.append(ToSizeString(options_->max_volume_size.value()))
        .append(") is smaller than target_volume_size(")
        .append(ToSizeString(options_->target_volume_size.value()))
        .append(").");
    return fit::error(error);
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
      return fit::error(error);
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
          return fit::error("Address descriptor of " + partition.volume().name +
                            " contains overlapping mappings. Conflict between " +
                            mapping.DebugString() + " and " + current_extent->DebugString());
        }
      }
      extents.insert(&mapping);
      accumulated_slices_ += GetBlockCount(mapping.target, mapping.count, options_->slice_size);
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
    return fit::error(error);
  }

  descriptor.metadata_required_size_ = metadata_allocated_size_;
  descriptor.slice_count_ = accumulated_slices_;
  descriptor.options_ = std::move(options_.value());

  return fit::ok(std::move(descriptor));
}

}  // namespace storage::volume_image
