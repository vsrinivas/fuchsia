// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_IMAGE_EXTEND_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_IMAGE_EXTEND_H_

#include <cstdint>

#include "lib/fpromise/result.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// Returns |fpromise::ok| if the header from |source_image| was parsed correctly.
//
// On error, returns a string describing the error conditions.
fpromise::result<uint64_t, std::string> FvmImageGetSize(const Reader& source_image);

// Returns |fpromise::ok| if the |source_image| has successfully been written into |target_image|
// with the updated |options|.
//
// Supported Options:
//  - |target_volume_size| updates the metadata size and the usable slices as well.
//
// On error, returns a string describing the error conditions.
fpromise::result<void, std::string> FvmImageExtend(const Reader& source_image,
                                                   const FvmOptions& options, Writer& target_image);

// Returns the trimmed sized of a raw fvm image. If the resource containing the fvm image, is
// truncated to the trimmed sized, all the trailing data on unallocated slices are removed.
//
// For example, a 14 terabyte fvm image with 1 MB worth of data, could be trimmed to a coupole of
// MBs(metadata and the actual data).
//
// An error is returned if the fvm image contains invalid metadata.
fpromise::result<uint64_t, std::string> FvmImageGetTrimmedSize(const Reader& source_image);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_IMAGE_EXTEND_H_
