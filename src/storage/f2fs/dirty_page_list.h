// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_DIRTY_PAGE_LIST_H_
#define SRC_STORAGE_F2FS_DIRTY_PAGE_LIST_H_

#include <lib/zx/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <fbl/intrusive_double_list.h>

#include "src/lib/storage/vfs/cpp/shared_mutex.h"
#include "src/storage/f2fs/file_cache.h"

namespace f2fs {

class DirtyPageList {
 public:
  DirtyPageList() = default;
  DirtyPageList(const DirtyPageList &) = delete;
  DirtyPageList &operator=(const DirtyPageList &) = delete;
  DirtyPageList(const DirtyPageList &&) = delete;
  DirtyPageList &operator=(const DirtyPageList &&) = delete;
  ~DirtyPageList();

  void Reset() __TA_EXCLUDES(list_lock_);

  zx::result<> AddDirty(LockedPage &page) __TA_EXCLUDES(list_lock_);
  zx::result<> RemoveDirty(LockedPage &page) __TA_EXCLUDES(list_lock_);

  uint64_t Size() const __TA_EXCLUDES(list_lock_) {
    fs::SharedLock read_lock(list_lock_);
    return dirty_list_.size();
  }

  std::vector<LockedPage> TakePages(size_t count) __TA_EXCLUDES(list_lock_);

 private:
  mutable fs::SharedMutex list_lock_{};
  PageList dirty_list_ __TA_GUARDED(list_lock_){};
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_DIRTY_PAGE_LIST_H_
