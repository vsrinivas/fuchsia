// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_H_

#include <fvm/fvm-sparse.h>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/utils/compressor.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace fvm_sparse_internal {

// Returns a bit set representing the suported options in the |fvm::SparseImage| that are equivalent
// in |FvmOptions|.
uint32_t GetImageFlags(const FvmOptions& options);

// Returns a bit set representing the suported options in the |fvm::PartitionDescriptor| that are
// equivalent in |Partition|.
uint32_t GetPartitionFlags(const Partition& partition);

}  // namespace fvm_sparse_internal

// Represents a |Partition| in the |fvm::SparseImage| format.
struct FvmSparsePartitionEntry {
  // Describes a partition, with things like name, guid and flags.
  fvm::PartitionDescriptor descriptor;

  // Describes each extent individually.
  std::vector<fvm::ExtentDescriptor> extents;
};

// Returns a |fvm::SparseImage| representation of |descriptor| on success.
fvm::SparseImage FvmSparseGenerateHeader(const FvmDescriptor& descriptor);

// Returns a |FvmSparsePartitionEntry| representation of |partition| on success.
fit::result<FvmSparsePartitionEntry, std::string> FvmSparseGeneratePartitionEntry(
    uint64_t slice_size, const Partition& partition);

// Returns the size in bytes of the generated sparse image for |descriptor|.
uint64_t FvmSparseCalculateUncompressedImageSize(const FvmDescriptor& descriptor);

// Returns the size of the written image in bytes when successfully writing a |SparseImage| and
// its data with |writer|.
fit::result<uint64_t, std::string> FvmSparseWriteImage(const FvmDescriptor& descriptor,
                                                       Writer* writer,
                                                       Compressor* compressor = nullptr);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_H_
