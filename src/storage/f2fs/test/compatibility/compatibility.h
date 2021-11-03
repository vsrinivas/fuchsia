// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_
#define SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

std::string GenerateTestPath(std::string_view format);

zx::status<std::pair<std::unique_ptr<F2fs>, fbl::RefPtr<Dir>>> CreateFsAndRootFromImage(
    MountOptions mount_options, fbl::unique_fd fd, uint64_t block_count);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_
