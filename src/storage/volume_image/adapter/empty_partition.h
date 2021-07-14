// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_ADAPTER_EMPTY_PARTITION_H_
#define SRC_STORAGE_VOLUME_IMAGE_ADAPTER_EMPTY_PARTITION_H_

#include <lib/fpromise/result.h>

#include <string>

#include "src/storage/volume_image/adapter/adapter_options.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/partition.h"

namespace storage::volume_image {

// Returns a Partition to be incorporated into a FVM image, containing .
//
// Note: Eventually as blobfs host tool gets cleaned up, it should generate the volume and address
// descriptor for blobfs, in the meantime we generate them on the fly.
fpromise::result<Partition, std::string> CreateEmptyFvmPartition(
    const PartitionOptions& partition_options, const FvmOptions& fvm_options);

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_ADAPTER_EMPTY_PARTITION_H_
