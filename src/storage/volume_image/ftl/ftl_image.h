// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IMAGE_H_
#define SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IMAGE_H_

#include <lib/fit/result.h>

#include <string>

#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/partition.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
// Returns |fit::ok|, when the contents of |partition| have been written into |writer| as a
// |RawNandImage|.
//
// It is required for |writer| to manage gaps in unwritten parts. For example, a raw block image may
// choose to zero the contents of the unwritten parts, while a sparse format may just keep track of
// the ranges.
fit::result<void, std::string> FtlImageWrite(const RawNandOptions& options,
                                             const Partition& partition, Writer* writer);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IMAGE_H_
