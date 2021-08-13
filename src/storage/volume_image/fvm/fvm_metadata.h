// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_METADATA_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_METADATA_H_

#include "lib/fpromise/result.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/volume_image/utils/reader.h"

namespace storage::volume_image {

// Returns an |fvm::Metadata| object using the reader for an fvm image.
fpromise::result<fvm::Metadata, std::string> FvmGetMetadata(const Reader& source_image);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_METADATA_H_
