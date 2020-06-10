// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_descriptor.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <fbl/algorithm.h>
#include <fvm/format.h>

#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/extent.h"

namespace storage::volume_image {
namespace {

std::string ToSizeString(uint64_t bytes) {
  constexpr int kByteToMegabyte = 2 << 20;
  std::string size_str = std::to_string(static_cast<double>(bytes) / kByteToMegabyte);
  return size_str.append(" [MB]");
}

}  // namespace

namespace internal {

uint64_t GetMetadataSize(const FvmOptions& options, uint64_t slice_count) {
  uint64_t required_metadata_allocated_size = 0;

  if (options.max_volume_size.has_value()) {
    required_metadata_allocated_size =
        fvm::MetadataSize(options.max_volume_size.value(), options.slice_size);
  } else if (options.target_volume_size.has_value()) {
    required_metadata_allocated_size =
        fvm::MetadataSize(options.target_volume_size.value(), options.slice_size);
  } else {
    required_metadata_allocated_size =
        2 * (fvm::AllocationTable::kOffset +
             fbl::round_up(slice_count * sizeof(fvm::SliceEntry), fvm::kBlockSize));
  }

  return required_metadata_allocated_size;
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
      std::string error = "Partition already exists, could not add partition ";
      error.append(partition.volume().name)
          .append(" and instance guid ")
          .append(Guid::ToString(partition.volume().instance).value())
          .append(" failed.\n Partition");

      error.append(it->volume().name)
          .append(" and instance guid ")
          .append(Guid::ToString(it->volume().instance).value())
          .append(" was added before.");
      partitions_.clear();
      return fit::error(error);
    }

    // Update total slices accounted for.
    for (const auto& mapping : partition.address().mappings) {
      Extent extent(mapping.source, mapping.count, partition.volume().block_size);
      auto [slice_extents, tail] = extent.Convert(mapping.target, options_->slice_size);
      accumulated_slices_ += slice_extents.count();
    }

    descriptor.partitions_.emplace(std::move(partition));
  }
  partitions_.clear();

  metadata_allocated_size_ = internal::GetMetadataSize(*options_, accumulated_slices_);
  uint64_t minimum_size = metadata_allocated_size_ + accumulated_slices_ * options_->slice_size;
  // We are not allowed to exceed the  target disk size when set.
  if (minimum_size > options_->target_volume_size.value_or(std::numeric_limits<uint64_t>::max())) {
    std::string error =
        "Failed to build FVMDescriptor. Image does not fit in target volume size. Minimum size is ";
    error.append(ToSizeString(minimum_size))
        .append(" and target size is ")
        .append(ToSizeString(options_->target_volume_size.value()))
        .append(".");
    return fit::error(error);
  }

  descriptor.metadata_required_size_ = metadata_allocated_size_;
  descriptor.slice_count_ = accumulated_slices_;
  descriptor.options_ = std::move(options_.value());

  return fit::ok(std::move(descriptor));
}

}  // namespace storage::volume_image
