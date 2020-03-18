// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_OPTIONS_H_
#define SRC_STORAGE_VOLUME_IMAGE_OPTIONS_H_

#include <unordered_map>

namespace storage::volume_image {

// Supported compression schemas for the generated block images.
enum class CompressionSchema : uint64_t {
  kNone = 0,
  kLz4 = 1,
};

// Supported encryption mechanisms for the generated images.
enum class EncryptionType : uint64_t {
  kNone = 0,
  kZxcrypt = 1,
};

// Supported options for partitions.
enum class Option : uint64_t {
  kNone = 0,
  kEmpty = 1,
};

struct CompressionOptions {
  // Compression type used.
  CompressionSchema schema;

  // 'schema' specific options and parameters.
  std::unordered_map<std::string, uint64_t> options;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_OPTIONS_H_
