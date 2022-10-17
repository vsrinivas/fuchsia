// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/options.h"

#include <string>

#include "fidl/fuchsia.fs.startup/cpp/wire_types.h"

namespace fs_management {

std::vector<std::string> MountOptions::as_argv(const char *binary) const {
  std::vector<std::string> argv;
  argv.push_back(binary);
  if (verbose_mount) {
    argv.push_back("--verbose");
  }

  argv.push_back("mount");

  if (readonly) {
    argv.push_back("--readonly");
  }
  if (write_compression_algorithm) {
    argv.push_back("--compression");
    argv.push_back(*write_compression_algorithm);
  }
  if (write_compression_level >= 0) {
    argv.push_back("--compression_level");
    argv.push_back(std::to_string(write_compression_level));
  }
  if (cache_eviction_policy) {
    argv.push_back("--eviction_policy");
    argv.push_back(*cache_eviction_policy);
  }
  if (fsck_after_every_transaction) {
    argv.push_back("--fsck_after_every_transaction");
  }
  if (sandbox_decompression) {
    argv.push_back("--sandbox_decompression");
  }
  return argv;
}

zx::result<fuchsia_fs_startup::wire::StartOptions> MountOptions::as_start_options() const {
  fuchsia_fs_startup::wire::StartOptions options;

  options.read_only = readonly;
  options.verbose = verbose_mount;
  options.sandbox_decompression = sandbox_decompression;
  options.write_compression_level = write_compression_level;

  if (write_compression_algorithm) {
    if (*write_compression_algorithm == "ZSTD_CHUNKED") {
      options.write_compression_algorithm =
          fuchsia_fs_startup::wire::CompressionAlgorithm::kZstdChunked;
    } else if (*write_compression_algorithm == "UNCOMPRESSED") {
      options.write_compression_algorithm =
          fuchsia_fs_startup::wire::CompressionAlgorithm::kUncompressed;
    } else {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  } else {
    options.write_compression_algorithm =
        fuchsia_fs_startup::wire::CompressionAlgorithm::kZstdChunked;
  }

  if (cache_eviction_policy) {
    if (*cache_eviction_policy == "NEVER_EVICT") {
      options.cache_eviction_policy_override =
          fuchsia_fs_startup::wire::EvictionPolicyOverride::kNeverEvict;
    } else if (*cache_eviction_policy == "EVICT_IMMEDIATELY") {
      options.cache_eviction_policy_override =
          fuchsia_fs_startup::wire::EvictionPolicyOverride::kEvictImmediately;
    } else if (*cache_eviction_policy == "NONE") {
      options.cache_eviction_policy_override =
          fuchsia_fs_startup::wire::EvictionPolicyOverride::kNone;
    } else {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  } else {
    options.cache_eviction_policy_override =
        fuchsia_fs_startup::wire::EvictionPolicyOverride::kNone;
  }

  return zx::ok(options);
}

std::vector<std::string> MkfsOptions::as_argv(const char *binary) const {
  std::vector<std::string> argv;
  argv.push_back(binary);

  if (verbose) {
    argv.push_back("-v");
  }

  MkfsOptions default_options;  // Use to get the default value.
  if (fvm_data_slices > default_options.fvm_data_slices) {
    argv.push_back("--fvm_data_slices");
    argv.push_back(std::to_string(fvm_data_slices));
  }

  if (deprecated_padded_blobfs_format) {
    argv.push_back("--deprecated_padded_format");
  }

  if (num_inodes > 0) {
    argv.push_back("--num_inodes");
    argv.push_back(std::to_string(num_inodes));
  }

  argv.push_back("mkfs");

  return argv;
}

fuchsia_fs_startup::wire::FormatOptions MkfsOptions::as_format_options() const {
  fuchsia_fs_startup::wire::FormatOptions options;

  options.verbose = verbose;
  options.deprecated_padded_blobfs_format = deprecated_padded_blobfs_format;
  options.num_inodes = num_inodes;

  return options;
}

std::vector<std::string> FsckOptions::as_argv(const char *binary) const {
  std::vector<std::string> argv;
  argv.push_back(binary);
  if (verbose) {
    argv.push_back("-v");
  }
  // TODO(smklein): Add support for modify, force flags. Without them,
  // we have "never_modify=true" and "force=true" effectively on by default.
  argv.push_back("fsck");

  return argv;
}

std::vector<std::string> FsckOptions::as_argv_fat32(const char *binary,
                                                    const char *device_path) const {
  std::vector<std::string> argv;
  argv.push_back(binary);
  if (never_modify) {
    argv.push_back("-n");
  } else if (always_modify) {
    argv.push_back("-y");
  }
  if (force) {
    argv.push_back("-f");
  }
  argv.push_back(device_path);

  return argv;
}

fuchsia_fs_startup::wire::CheckOptions FsckOptions::as_check_options() const {
  return fuchsia_fs_startup::wire::CheckOptions{};
}

}  // namespace fs_management
