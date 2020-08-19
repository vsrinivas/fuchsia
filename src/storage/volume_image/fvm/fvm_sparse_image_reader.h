// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_READER_H_
#define SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_READER_H_

#include <lib/fit/result.h>

#include <string>

#include "src/storage/volume_image/partition.h"
#include "src/storage/volume_image/utils/reader.h"

namespace storage::volume_image {

// Opens an existing FVM sparse image and exposes a partition that looks just as it would on the
// device i.e. if you were to serialize it to a block device, FVM would recognise it.  At this time
// the reader embedded within the partition can only support sequential reads (which is all we need
// to support at this time).
fit::result<Partition, std::string> OpenSparseImage(Reader& base_reader);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FVM_FVM_SPARSE_IMAGE_READER_H_
