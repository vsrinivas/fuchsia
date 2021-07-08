// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_TEST_MEMFS_FS_TEST_H_
#define SRC_STORAGE_MEMFS_TEST_MEMFS_FS_TEST_H_

#include "src/storage/fs_test/fs_test.h"

namespace memfs {

__EXPORT fs_test::TestFilesystemOptions DefaultMemfsTestOptions();

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_TEST_MEMFS_FS_TEST_H_
