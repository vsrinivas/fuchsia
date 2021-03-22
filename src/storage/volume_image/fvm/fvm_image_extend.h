// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_IMAGE_EXTEND_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_IMAGE_EXTEND_H_

#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// Returns |fit::ok| if the |source_image| has successfully been written into |target_image| with
// the updated |options|.
//
// Supported Options:
//  - |target_volume_size| updates the metadata size and the usable slices as well.
//
// On error, returns a string describing the error conditions.
fit::result<void, std::string> FvmImageExtend(const Reader& source_image, const FvmOptions& options,
                                              Writer& target_image);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_IMAGE_EXTEND_H_
