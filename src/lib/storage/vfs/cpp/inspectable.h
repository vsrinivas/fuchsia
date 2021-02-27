// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INSPECTABLE_H_
#define SRC_LIB_STORAGE_VFS_CPP_INSPECTABLE_H_

#include <zircon/types.h>

namespace fs {

using blk_t = uint32_t;

class Inspectable {
 public:
  virtual ~Inspectable() = default;

  // Reads a block at the |start_block_num| location.
  virtual zx_status_t ReadBlock(blk_t start_block_num, void* out_data) const = 0;
};

}  //  namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_INSPECTABLE_H_
