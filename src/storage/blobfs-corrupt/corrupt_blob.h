// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_CORRUPT_CORRUPT_BLOB_H_
#define SRC_STORAGE_BLOBFS_CORRUPT_CORRUPT_BLOB_H_

#include <blobfs/common.h>
#include <block-client/cpp/block-device.h>

struct BlobCorruptOptions {
  blobfs::Digest merkle;
};

// Corrupts the contents of the given blob within the given blobfs formatted block device. Returns
// success iff the image is in a clean state and the requested blob was corrupted.
zx_status_t CorruptBlob(std::unique_ptr<block_client::BlockDevice> device,
                        BlobCorruptOptions* options);

#endif  // SRC_STORAGE_BLOBFS_CORRUPT_CORRUPT_BLOB_H_
