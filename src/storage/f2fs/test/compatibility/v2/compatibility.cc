// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/test/compatibility/v2/compatibility.h"

namespace f2fs {

void LinuxOperator::Mkfs(std::string_view opt) {
  std::string result;
  ASSERT_EQ(debian_guest_->Execute({"mkfs.f2fs", test_device_, "-f"}, {}, zx::time::infinite(),
                                   &result, nullptr),
            ZX_OK);
}

void LinuxOperator::Fsck() {
  std::string result;
  ASSERT_EQ(debian_guest_->Execute({"fsck.f2fs", test_device_, "--dry-run"}, {},
                                   zx::time::infinite(), &result, nullptr),
            ZX_OK);
}

void FuchsiaOperator::Mkfs(MkfsOptions opt) {
  MkfsWorker mkfs(std::move(bc_), opt);
  auto ret = mkfs.DoMkfs();
  ASSERT_TRUE(ret.is_ok());
  bc_ = std::move(*ret);
}

void FuchsiaOperator::Fsck() {
  FsckWorker fsck(std::move(bc_), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.Run(), ZX_OK);
  bc_ = fsck.Destroy();
}

}  // namespace f2fs
