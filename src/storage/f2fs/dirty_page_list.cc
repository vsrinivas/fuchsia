// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <mutex>

#include <fbl/ref_ptr.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

DirtyPageList::~DirtyPageList() {
  std::lock_guard list_lock(list_lock_);
  ZX_ASSERT(dirty_list_.is_empty());
}

void DirtyPageList::Reset() {
  std::lock_guard list_lock(list_lock_);
  dirty_list_.clear();
}

zx_status_t DirtyPageList::AddDirty(Page* page) {
  ZX_ASSERT(page != nullptr);
  ZX_DEBUG_ASSERT(page->InTreeContainer());
  ZX_DEBUG_ASSERT(page->IsActive());
  ZX_DEBUG_ASSERT(page->IsDirty());
  std::lock_guard lock(list_lock_);
  if (page->InListContainer()) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  dirty_list_.push_back(page);
  return ZX_OK;
}

zx_status_t DirtyPageList::RemoveDirty(Page* page) {
  ZX_ASSERT(page != nullptr);
  std::lock_guard lock(list_lock_);
  if (!page->InListContainer()) {
    return ZX_ERR_NOT_FOUND;
  }
  dirty_list_.erase(*page);
  return ZX_OK;
}

std::vector<LockedPage> DirtyPageList::TakePages(size_t count) {
  std::vector<LockedPage> dirty_pages;
  std::lock_guard lock(list_lock_);
  size_t try_count = std::min(count, dirty_list_.size());
  dirty_pages.reserve(try_count);
  while (!dirty_list_.is_empty() && try_count--) {
    auto page = dirty_list_.pop_front();
    ZX_DEBUG_ASSERT(page != nullptr);
    fbl::RefPtr<Page> dirty_page(page);
    LockedPage locked_page(std::move(dirty_page));
    dirty_pages.push_back(std::move(locked_page));
  }
  return dirty_pages;
}

}  // namespace f2fs
