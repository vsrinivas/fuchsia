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

// Returns a bit set representing the suported options in the |fvm::sparse_image_t|
// that are equivalent in |FvmOptions|.
uint32_t GetImageFlags(const FvmOptions& options);

// Returns a bit set representing the suported options in the |fvm::partition_descriptor_t|
// that are equivalent in |Partition|.
uint32_t GetPartitionFlags(const Partition& partition);

}  // namespace fvm_sparse_internal

// Represents a |Partition| in the |fvm::sparse_image_t| format.
struct FvmSparsePartitionEntry {
  // Describes a partition, with things like name, guid and flags.
  fvm::partition_descriptor_t descriptor;

  // Describes each extent individually.
  std::vector<fvm::extent_descriptor_t> extents;
};

// Returns a |fvm::sparse_image_t| representation of |descriptor| on success.
fvm::sparse_image_t FvmSparseGenerateHeader(const FvmDescriptor& descriptor);

// Returns a |FvmSparsePartitionEntry| representation of |partition| on success.
FvmSparsePartitionEntry FvmSparseGeneratePartitionEntry(uint64_t slice_size,
                                                        const Partition& partition);

// Returns the size in bytes of the generated sparse image for |descriptor|.
uint64_t FvmSparseCalculateUncompressedImageSize(const FvmDescriptor& descriptor);

// Returns the size of the written image in bytes when successfully writing a |sparse_image_t| and
// its data with |writer|.
fit::result<uint64_t, std::string> FvmSparseWriteImage(const FvmDescriptor& descriptor,
                                                       Writer* writer,
                                                       Compressor* compressor = nullptr);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_H_
