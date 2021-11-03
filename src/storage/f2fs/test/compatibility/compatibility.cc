// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/test/compatibility/compatibility.h"

#include <filesystem>

namespace f2fs {

std::string GenerateTestPath(std::string_view format) {
  std::string test_path = std::filesystem::temp_directory_path().append(format).string();
  return test_path;
}

zx::status<std::pair<std::unique_ptr<F2fs>, fbl::RefPtr<Dir>>> CreateFsAndRootFromImage(
    MountOptions mount_options, fbl::unique_fd fd, uint64_t block_count) {
  std::unique_ptr<Bcache> bcache;
  std::unique_ptr<F2fs> fs;
  fbl::RefPtr<VnodeF2fs> root;

  if (auto result = Bcache::Create(std::move(fd), block_count, &bcache); result != ZX_OK) {
    return zx::error(result);
  }

  if (auto result = Fsck(std::move(bcache), &bcache); result != ZX_OK) {
    return zx::error(result);
  }

  if (auto result = F2fs::Create(std::move(bcache), MountOptions{}, &fs); result != ZX_OK) {
    return zx::error(result);
  }

  if (auto result = VnodeF2fs::Vget(fs.get(), fs->RawSb().root_ino, &root); result != ZX_OK) {
    return zx::error(result);
  }

  if (auto result =
          root->Open(root->ValidateOptions(fs::VnodeConnectionOptions()).value(), nullptr);
      result != ZX_OK) {
    return zx::error(result);
  }

  return zx::ok(std::pair<std::unique_ptr<F2fs>, fbl::RefPtr<Dir>>{
      std::move(fs), fbl::RefPtr<Dir>::Downcast(std::move(root))});
}

}  // namespace f2fs
