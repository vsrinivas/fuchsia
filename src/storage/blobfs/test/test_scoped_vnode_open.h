// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_TEST_SCOPED_VNODE_OPEN_H_
#define SRC_STORAGE_BLOBFS_TEST_TEST_SCOPED_VNODE_OPEN_H_

#include <gtest/gtest.h>

#include "src/lib/storage/vfs/cpp/scoped_vnode_open.h"

namespace blobfs {

// Simple wrapper around fs::ScopedVnodeOpen that EXPECTs all calls to succeed.
class TestScopedVnodeOpen {
 public:
  // This uses an explicit Open() call so errors can be reported.
  explicit TestScopedVnodeOpen(
      fs::Vnode* vn, const fs::VnodeConnectionOptions& opts = fs::VnodeConnectionOptions()) {
    EXPECT_EQ(ZX_OK, opener_.Open(vn, opts));
  }

  template <typename Node>
  explicit TestScopedVnodeOpen(
      const fbl::RefPtr<Node>& node,
      const fs::VnodeConnectionOptions& opts = fs::VnodeConnectionOptions()) {
    EXPECT_EQ(ZX_OK, opener_.Open(node, opts));
  }

  ~TestScopedVnodeOpen() { EXPECT_EQ(ZX_OK, opener_.Close()); }

 private:
  fs::ScopedVnodeOpen opener_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_TEST_SCOPED_VNODE_OPEN_H_
