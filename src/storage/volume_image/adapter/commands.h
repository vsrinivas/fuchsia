// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_ADAPTER_COMMANDS_H_
#define SRC_STORAGE_VOLUME_IMAGE_ADAPTER_COMMANDS_H_

#include <lib/fpromise/result.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fbl/span.h>

#include "src/storage/volume_image/adapter/adapter_options.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/guid.h"

// This header provides an entry point for CLI tools, such that CLI's job is just mapping arguments
// to parameters.
// This functions add support for FVM legacy host tool. Eventually all of this should be removed,
// and rely on the json schema described on serialization, allowing for a full plug in method.
namespace storage::volume_image {

enum class Command {
  kCreate,
  kCreateSparse,
  kPave,
  kExtend,
  kSize,
  kUnsupported,
};

// For a given string returns the associated |Command|.
Command CommandFromString(std::string_view command_str);

// Output image format.
enum class FvmImageFormat {
  // Produces a fvm image that can be mounted as a block device.
  kBlockImage,

  // Produces a sparse image for the FVM, that needs to be paved into a container(file, device)
  // in order to be mounted. Useful for transmitting.
  kSparseImage,
};

// Supported image formats.
enum PartitionImageFormat {
  kBlobfs,
  kMinfs,
  kEmptyPartition,
};

struct PartitionParams {
  static fpromise::result<std::vector<PartitionParams>, std::string> FromArguments(
      fbl::Span<std::string_view> arguments, const FvmOptions& options);

  // The image path for the partition.
  std::string source_image_path;

  // Label to be used by the volume. If not the default one.
  std::string label;

  // Sets the type guide of the generated partition.
  // Only supported for
  std::optional<std::array<uint8_t, kGuidLength>> type_guid;

  // Whether the volume should be flagged as encrypted.
  // Only supported for Format::Sparse.
  bool encrypted = false;

  // Custom partition options.
  PartitionOptions options;

  // For empty partitions, describes the range of slices to allocate.
  PartitionImageFormat format;
};

struct CreateParams {
  // Returns arguments from |arguments| as a |CreateParam| instance. Validation is done by the
  // |CreateParam| consumers.
  static fpromise::result<CreateParams, std::string> FromArguments(
      fbl::Span<std::string_view> arguments);

  // Path to the output file where the FVM image should be written to.
  std::string output_path;

  // Embedded output.
  // The contents are written into an embedded image, this just enforced
  // a maximum size and strict bound checking when writing. If the image would
  // exceed the provided length at any point, it will be treated as a hard failure.
  bool is_output_embedded = false;

  // When in an embedded output, this is the starting point of the image.
  std::optional<uint64_t> offset;

  // When set provides a hard maximum on the generated image 'expanded' size, that is
  // a sparse image when paved, cannot exceed such length. This consists on a limit
  // to the metadata and allocated slices size.
  std::optional<uint64_t> length;

  // Output fvm image format.
  FvmImageFormat format;

  // Information about the partitons to be created.
  std::vector<PartitionParams> partitions;

  // Information about the FVM.
  FvmOptions fvm_options;

  // When set the image will be trimmed to remove all unallocated slices from the tail.
  bool trim_image = false;
};

// Creates an fvm image according to |params| and |options|.
fpromise::result<void, std::string> Create(const CreateParams& params);

enum class TargetType {
  // Device is a Memory Techonology Device. (Raw Nand)
  kMtd,

  // Device is a block device.
  kBlockDevice,

  // Path points towards a file or character device.
  kFile,
};

struct PaveParams {
  // Returns arguments from |arguments| as a |PaveParams| instance. Validation is done by the
  // |PaveParams| consumers.
  static fpromise::result<PaveParams, std::string> FromArguments(
      fbl::Span<std::string_view> arguments);

  // Sparse image path.
  std::string input_path;

  // Protocol to use on the FD of |target_path|.
  TargetType type;

  // Path to be paved.
  std::string output_path;

  // Embedded output.
  // The contents are written into an embedded image, this just enforced
  // a maximum size and strict bound checking when writing. If the image would
  // exceed the provided length at any point, it will be treated as a hard failure.
  bool is_output_embedded = false;

  // When in an embedded output, this is the starting point of the image.
  std::optional<uint64_t> offset;

  // When set provides a hard maximum on the generated image 'expanded' size, that is
  // a sparse image when paved, cannot exceed such length. This consists on a limit
  // to the metadata and allocated slices size.
  std::optional<uint64_t> length;

  // Maximum number of bad blocks in the underlying MTD device.
  // This is required parameter for |type| = |kMtdDevice|.
  std::optional<uint64_t> max_bad_blocks;

  // Pave options for the source image.
  FvmOptions fvm_options;
};

// Given an input sparse fvm image, it will write the expanded contents to the path.
fpromise::result<void, std::string> Pave(const PaveParams& params);

struct ExtendParams {
  // Returns arguments from |arguments| as a |ExtendParams| instance. Validation is done by the
  // |ExtendParams| consumers.
  static fpromise::result<ExtendParams, std::string> FromArguments(
      fbl::Span<std::string_view> arguments);

  // Path to the file where the FVM image is contained.
  std::string image_path;

  // When set provides a hard maximum on the generated image 'expanded' size, that is
  // a sparse image when paved, cannot exceed such length. This consists on a limit
  // to the metadata and allocated slices size.
  std::optional<uint64_t> length;

  // After modifying the image, remove trailing unallocated slices from the slice region without
  // changing the metadata.
  bool should_trim = false;

  // If true, will pick as |fvm::Header::fvm_partition_size| to use the maximum between the provided
  // |length| and the existing image |fvm::Header::fvm_partition_size|.
  bool should_use_max_partition_size = false;
};

fpromise::result<void, std::string> Extend(const ExtendParams& params);

struct SizeParams {
  // Returns arguments from |arguments| as a |SizeParams| instance. Validation is done by the
  // |ExtendParams| consumers.
  static fpromise::result<SizeParams, std::string> FromArguments(
      fbl::Span<std::string_view> arguments);

  // Path to the file where the FVM sparse image is contained.
  std::string image_path;

  // When provided the target image allocated slice count, should fit in a fvm image of |length|.
  std::optional<uint64_t> length;
};

// Returns the allocated data size(metadata and slices) within a sparse image.
//
// If |length| is set an error is returned if the sparse image cannot be paved in a disk size of
// |length|.
//
// On error returns a string describing the error.
fpromise::result<uint64_t, std::string> Size(const SizeParams& params);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_ADAPTER_COMMANDS_H_
