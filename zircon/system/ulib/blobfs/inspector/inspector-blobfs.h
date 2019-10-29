// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_INSPECTOR_BLOBFS_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_INSPECTOR_BLOBFS_H_

#include <blobfs/format.h>
#include <fbl/macros.h>

namespace blobfs {

// Interface for blobfs to implement for the inspector to get information
// about the underlying blobfs. Additional methods should be added here
// and implemented in blobfs to expose more information.
// This interface should only be used for blobfs disk-inspect functionality.
class InspectorBlobfs {
 public:
  virtual ~InspectorBlobfs() = default;

  // Returns an immutable reference to the superblock
  virtual const Superblock& GetSuperblock() const = 0;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_INSPECTOR_BLOBFS_H_
