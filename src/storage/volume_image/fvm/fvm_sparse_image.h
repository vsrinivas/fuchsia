// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_H_

#include <lib/fpromise/result.h>

#include <cstdint>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_sparse.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/compressor.h"
#include "src/storage/volume_image/utils/decompressor.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace fvm_sparse_internal {

// Returns a bit set representing the suported options in the |fvm::SparseImage| that are equivalent
// in |FvmOptions|.
uint32_t GetImageFlags(const FvmOptions& options);

// Returns a bit set representing the suported options in the |fvm::PartitionDescriptor| that are
// equivalent in |Partition|.
uint32_t GetPartitionFlags(const Partition& partition);

// Represents a |Partition| in the |fvm::SparseImage| format.
struct PartitionEntry {
  // Describes a partition, with things like name, guid and flags.
  fvm::PartitionDescriptor descriptor;

  // Describes each extent individually.
  std::vector<fvm::ExtentDescriptor> extents;
};

// Returns the compressions options stored in |header|.
CompressionOptions GetCompressionOptions(const fvm::SparseImage& header);

// On success, returns the valid |fvm::SparseImage| header contained in |reader| starting at
// |offset|.
//
// On failure, returns the error which caused the header to be invalid.
fpromise::result<fvm::SparseImage, std::string> GetHeader(uint64_t offset, const Reader& reader);

// On success, returns the valid collection of |FvmSparsePartitionEntry| as described by |header|
// and contained in |reader| as starting at |offset|. That is, the partition descriptors start
// at |offset| in |reader|.
fpromise::result<std::vector<PartitionEntry>, std::string> GetPartitions(
    uint64_t offset, const Reader& reader, const fvm::SparseImage& header);

// Returns a non sparse |fvm::Header| from a sparse |header| with supported |options| overriden,
// and with a known number of initial slices.
//
// Supported options can be supplied for overriding those stored in the original header.
// Supported options:
//   - |max_volume_size|
//   - |target_volume_size
fpromise::result<fvm::Header, std::string> ConvertToFvmHeader(
    const fvm::SparseImage& sparse_header, uint64_t slice_count,
    const std::optional<FvmOptions>& options);

// Overload with no options by default.
inline fpromise::result<fvm::Header, std::string> ConvertToFvmHeader(
    const fvm::SparseImage& sparse_header, uint64_t slice_count) {
  return ConvertToFvmHeader(sparse_header, slice_count, std::nullopt);
}

fpromise::result<fvm::Metadata, std::string> ConvertToFvmMetadata(
    const fvm::Header& header, cpp20::span<const PartitionEntry> partition_entries);

// Returns a |fvm::SparseImage| representation of |descriptor| on success.
fvm::SparseImage GenerateHeader(const FvmDescriptor& descriptor);

// Returns a |FvmSparsePartitionEntry| representation of |partition| on success.
// If |extents_are_filled| is set to true, for each mapping in |partition| that has
// |AddressMapOption::kFill| set, the extent length will match the size of the extent, since the
// data will be expanded to include such values.
fpromise::result<PartitionEntry, std::string> GeneratePartitionEntry(
    uint64_t slice_size, const Partition& partition, bool extents_are_filled = false);

// Returns the size in bytes of the generated sparse image for |descriptor|.
uint64_t CalculateUncompressedImageSize(const FvmDescriptor& descriptor);

}  // namespace fvm_sparse_internal

// Returns the size of the written image in bytes when successfully writing a |SparseImage| and
// its data with |writer|.
fpromise::result<uint64_t, std::string> FvmSparseWriteImage(const FvmDescriptor& descriptor,
                                                            Writer* writer,
                                                            Compressor* compressor = nullptr);

// Returns true if |reader| is a compressed |fvm::SparseImage|, and has been successfully
// decompressed into |writer|. If not compressed returns false, and this is not considered an error.
//
// On error, returns a description of the error condition.
fpromise::result<bool, std::string> FvmSparseDecompressImage(uint64_t offset, const Reader& reader,
                                                             Writer& writer);

// Returns a |FvmDescriptor| representing the contained data in sparse image contained in |reader|
// starting at |offset|. |reader| must contain an uncompressed fvm sparse image, or an error is
// returned.
//
// On error, returns a description of the error condition.
fpromise::result<FvmDescriptor, std::string> FvmSparseReadImage(uint64_t offset,
                                                                std::unique_ptr<Reader> reader);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_H_
