// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_OPTIONS_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_OPTIONS_H_

#include <cstdint>
#include <optional>

#include "src/storage/volume_image/options.h"

namespace storage::volume_image {

// Sets the desired properties of the expected FVM.
struct FvmOptions {
  // If set, fvm creation should fail if the size exceeds this hard limit.
  std::optional<uint64_t> target_volume_size = std::nullopt;

  // If set, the fvm will preallocate enough metadata space to address a volume of |max_volume_size|
  // bytes. This allows the underlying partition to be extended. Because of this, |max_volume_size|
  // should always be greater than |target_volume_size| if set.
  std::optional<uint64_t> max_volume_size = std::nullopt;

  // Slice size used for the fvm image.
  // Must be non-zero.
  uint64_t slice_size = 0;

  // The compression scheme used for partition data in the generated image.
  CompressionOptions compression;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_OPTIONS_H_
