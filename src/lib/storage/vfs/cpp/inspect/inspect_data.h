// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the data types representing structured data which filesystems must expose.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECT_INSPECT_DATA_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECT_INSPECT_DATA_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/result.h>

#include <cstdint>
#include <optional>
#include <string>

#include "src/lib/storage/block_client/cpp/block_device.h"

namespace fs_inspect {

// fs.info properties
struct InfoData {
  uint64_t id;
  uint64_t type;
  std::string name;
  uint64_t version_major;
  uint64_t version_minor;
  uint64_t block_size;
  uint64_t max_filename_length;
  std::optional<std::string> oldest_version = std::nullopt;

  // Create an oldest_version string from integral version identifiers. Due to data collection
  // limitations, oldest_version must be stored as a string.
  static std::string OldestVersion(uint32_t oldest_major, uint32_t oldest_minor);

  // Inspect Property Names

  static constexpr char kPropId[] = "id";
  static constexpr char kPropType[] = "type";
  static constexpr char kPropName[] = "name";
  static constexpr char kPropVersionMajor[] = "version_major";
  static constexpr char kPropVersionMinor[] = "version_minor";
  static constexpr char kPropBlockSize[] = "block_size";
  static constexpr char kPropMaxFilenameLength[] = "max_filename_length";
  static constexpr char kPropOldestVersion[] = "oldest_version";
};

// fs.usage properties
struct UsageData {
  uint64_t total_bytes;
  uint64_t used_bytes;
  uint64_t total_nodes;
  uint64_t used_nodes;

  // Inspect Property Names

  static constexpr char kPropTotalBytes[] = "total_bytes";
  static constexpr char kPropUsedBytes[] = "used_bytes";
  static constexpr char kPropTotalNodes[] = "total_nodes";
  static constexpr char kPropUsedNodes[] = "used_nodes";
};

// fs.fvm properties (supported only for FVM-enabled filesystems)
struct FvmData {
  struct SizeInfo {
    // Current size of the volume that FVM has allocated for the filesystem.
    uint64_t size_bytes;
    // Size limit set on the volume, if any. If unset, value will be 0.
    uint64_t size_limit_bytes;
    // Amount of space the volume can be extended by. Based on the volume byte limit, if set,
    // otherwise the maximum amount of available slices.
    uint64_t available_space_bytes;
  } size_info;

  // Amount of times extending the volume failed when more space was required.
  uint64_t out_of_space_events;

  // Helper function to create a `SizeInfo` using the Fvm protocol from a block device.
  static zx::result<SizeInfo> GetSizeInfoFromDevice(const block_client::BlockDevice& device);

  // Inspect Property Names

  static constexpr char kPropSizeBytes[] = "size_bytes";
  static constexpr char kPropSizeLimitBytes[] = "size_limit_bytes";
  static constexpr char kPropAvailableSpaceBytes[] = "available_space_bytes";
  static constexpr char kPropOutOfSpaceEvents[] = "out_of_space_events";
};

// fs.volumes/{name} properties (supported only for multi-volume filesystems)
struct VolumeData {
  uint64_t used_bytes;
  std::optional<uint64_t> bytes_limit;
  uint64_t used_nodes;
  bool encrypted;

  // Inspect Property Names

  static constexpr char kPropVolumeUsedBytes[] = "used_bytes";
  static constexpr char kPropVolumeBytesLimit[] = "bytes_limit";
  static constexpr char kPropVolumeUsedNodes[] = "used_nodes";
  static constexpr char kPropVolumeEncrypted[] = "encrypted";
};

namespace detail {
// Attach the values from the given InfoData object as properties to the inspector's root node.
void Attach(inspect::Inspector& insp, const InfoData& info);

// Attach the values from the given UsageData object as properties to the inspector's root node.
void Attach(inspect::Inspector& insp, const UsageData& usage);

// Attach the values from the given FvmData object as properties to the inspector's root node.
void Attach(inspect::Inspector& insp, const FvmData& volume);
}  // namespace detail

}  // namespace fs_inspect

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECT_INSPECT_DATA_H_
