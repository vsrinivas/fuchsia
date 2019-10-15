// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functionality for checking the consistency of
// Blobfs.

#ifndef BLOBFS_HOST_FSCK_H_
#define BLOBFS_HOST_FSCK_H_

#include <memory>

#include <blobfs/host.h>

namespace blobfs {

zx_status_t Fsck(std::unique_ptr<Blobfs> blob, bool apply_journal);

}  // namespace blobfs

#endif  // BLOBFS_HOST_FSCK_H_
