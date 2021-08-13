// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_UNPACK_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_UNPACK_H_

#include <sys/types.h>

#include <optional>
#include <string>

#include "lib/fpromise/result.h"
#include "src/storage/volume_image/fvm/fvm_metadata.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// Unpacks an input raw fvm image writing all contained partitions using their internal names,
// with dashes replaced as underscores. De-duplicating names by appending a dash and numerical
// suffix to the 2nd or later copy of a name. Blank names will all be appended with a a dash and
// numerical suffix.
fpromise::result<void, std::string> UnpackRawFvm(const Reader& image,
                                                 const std::string& out_path_prefix);

namespace internal {
// Everything in the internal namespace is only exposed for testing.

// Unpacks an input raw fvm image writing partition ids to the associated |out_files| that match
// the index. Null writers or partition ids out of range will be ignored.
fpromise::result<void, std::string> UnpackRawFvmPartitions(
    const Reader& image, const fvm::Metadata& metadata,
    const std::vector<std::unique_ptr<Writer>>& out_files);

// Disambiguates duplicate names in a list by appending the 2nd or later copy of any entries with a
// dash and numerical suffix. All preexisting dashes will become underscores to preserve the dash
// as a separator. Blank names will all be appended with a a dash and numerical suffix.
std::vector<std::optional<std::string>> DisambiguateNames(
    const std::vector<std::optional<std::string>>& names);

}  // namespace internal

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_UNPACK_H_
