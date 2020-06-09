// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_DESCRIPTOR_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_DESCRIPTOR_H_

#include <lib/fit/result.h>

#include <set>
#include <string>
#include <vector>

#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/fvm/partition.h"
#include "src/storage/volume_image/options.h"

namespace storage::volume_image {
namespace internal {

// Returns the minimum size for addressing the a set of |slice_count| slices.
// Accounts for both primary and secondary control block.
uint64_t GetMetadataSize(const FvmOptions& options, uint64_t slice_count);

}  // namespace internal

// A FVM descriptor represents a collection of partitions and constraints that should eventually be
// converted into an image.
struct FvmDescriptor {
  // This class provides the mechanism for generating a valid FVM descriptor, so that constraints
  // can be verified.
  class Builder {
   public:
    Builder() = default;
    explicit Builder(FvmDescriptor descriptor);
    Builder(const Builder&) = delete;
    Builder(Builder&&) = default;
    Builder& operator=(const Builder&) = delete;
    Builder& operator=(Builder&&) = default;
    ~Builder() = default;

    // Adds partition to the image to be constructed.
    Builder& AddPartition(Partition partition);

    // Sets the options for the image to be constructed.
    Builder& SetOptions(const FvmOptions& options);

    // Verifies that constraints are met and returns an FvmDescriptor containing the data.
    //
    // This method will always consume all added partitions. On success the ownership is taken by
    // the returned descriptor, and on error the partitions are destroyed.
    fit::result<FvmDescriptor, std::string> Build();

   private:
    std::vector<Partition> partitions_;
    std::optional<FvmOptions> options_;
    uint64_t accumulated_slices_ = 0;
    uint64_t metadata_allocated_size_ = 0;
  };

  FvmDescriptor() = default;

  // Set of partitions that belong to the fvm.
  std::set<Partition, Partition::LessThan> partitions;

  // Options used to construct and validate this descriptor.
  FvmOptions options;

  // Number of slices required for this fvm descriptor.
  uint64_t slice_count = 0;

  // Size in bytes of the metadata required to generate this image.
  uint64_t metadata_required_size = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_DESCRIPTOR_H_
