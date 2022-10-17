// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__
#include "f2fs.h"

namespace f2fs {

DirEntryCache::DirEntryCache() {
  std::lock_guard lock(lock_);

  slab_allocator_ = std::make_unique<ElementAllocator>(kDirEntryCacheSlabCount, true);
}

DirEntryCache::~DirEntryCache() { Reset(); }

void DirEntryCache::Reset() {
  std::lock_guard lock(lock_);

  map_.clear();
  element_lru_list_.clear();
}

DirEntryCacheElement &DirEntryCache::AllocateElement(ino_t parent_ino,
                                                     std::string_view child_name) {
  ElementRefPtr element = slab_allocator_->New(parent_ino, child_name);
  if (element == nullptr) {
    Evict();
    element = slab_allocator_->New(parent_ino, child_name);
  }

  ZX_ASSERT(element != nullptr);

  map_[GenerateKey(parent_ino, child_name)] = element;

  OnCacheHit(element);

  return *element;
}

void DirEntryCache::DeallocateElement(ElementRefPtr element) {
  map_.erase(GenerateKey(element->GetParentIno(), element->GetName()));

  if (element->InContainer()) {
    element_lru_list_.erase(*element);
  }
}

ElementRefPtr DirEntryCache::FindElementRefPtr(ino_t parent_ino,
                                               std::string_view child_name) const {
  auto search = map_.find(GenerateKey(parent_ino, child_name));
  if (search == map_.end()) {
    return nullptr;
  }

  return search->second;
}

DirEntryCacheElement *DirEntryCache::FindElement(ino_t parent_ino, std::string_view child_name) {
  ElementRefPtr element = FindElementRefPtr(parent_ino, child_name);
  if (element == nullptr) {
    return nullptr;
  }

  OnCacheHit(element);

  return element.get();
}

void DirEntryCache::OnCacheHit(ElementRefPtr &element) {
  if (element->InContainer()) {
    element_lru_list_.erase(*element);
  }
  element_lru_list_.push_front(element);
}

void DirEntryCache::Evict() {
  if (!element_lru_list_.is_empty()) {
    DeallocateElement(element_lru_list_.pop_back());
  }
}

zx::result<DirEntry> DirEntryCache::LookupDirEntry(ino_t parent_ino, std::string_view child_name) {
  if (IsDotOrDotDot(child_name)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  std::lock_guard lock(lock_);

  DirEntryCacheElement *element = FindElement(parent_ino, child_name);
  if (element == nullptr) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  // The |element| may be evicted while the caller is using it.
  // Therefore, return copied value rather than reference.
  return zx::ok(element->GetDirEntry());
}

zx::result<pgoff_t> DirEntryCache::LookupDataPageIndex(ino_t parent_ino,
                                                       std::string_view child_name) {
  if (IsDotOrDotDot(child_name)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  std::lock_guard lock(lock_);

  DirEntryCacheElement *element = FindElement(parent_ino, child_name);
  if (element == nullptr) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  return zx::ok(element->GetDataPageIndex());
}

void DirEntryCache::AddNewDirEntry(ino_t parent_ino, std::string_view child_name,
                                   DirEntry &dir_entry, pgoff_t data_page_index) {
  DirEntryCacheElement &element = AllocateElement(parent_ino, child_name);

  element.SetDirEntry(dir_entry);
  element.SetDataPageIndex(data_page_index);
}

void DirEntryCache::UpdateDirEntry(ino_t parent_ino, std::string_view child_name,
                                   DirEntry &dir_entry, pgoff_t data_page_index) {
  if (IsDotOrDotDot(child_name)) {
    return;
  }

  std::lock_guard lock(lock_);

  DirEntryCacheElement *element = FindElement(parent_ino, child_name);
  if (element == nullptr) {
    AddNewDirEntry(parent_ino, child_name, dir_entry, data_page_index);
    return;
  }

  element->SetDirEntry(dir_entry);
  element->SetDataPageIndex(data_page_index);
}

void DirEntryCache::RemoveDirEntry(ino_t parent_ino, std::string_view child_name) {
  if (IsDotOrDotDot(child_name)) {
    return;
  }

  std::lock_guard lock(lock_);

  ElementRefPtr element = FindElementRefPtr(parent_ino, child_name);
  if (element != nullptr) {
    DeallocateElement(std::move(element));
  }
}

bool DirEntryCache::IsElementInCache(ino_t parent_ino, std::string_view child_name) const {
  std::lock_guard lock(lock_);
  auto search = map_.find(DirEntryCache::GenerateKey(parent_ino, child_name));
  if (search == map_.end()) {
    return false;
  }
  return true;
}

bool DirEntryCache::IsElementAtHead(ino_t parent_ino, std::string_view child_name) const {
  std::lock_guard lock(lock_);
  if (element_lru_list_.is_empty()) {
    return false;
  }

  auto element = element_lru_list_.begin();
  return (element->GetParentIno() == parent_ino) && (element->GetName() == child_name);
}

const std::map<EntryKey, ElementRefPtr> &DirEntryCache::GetMap() const {
  std::lock_guard lock(lock_);
  return map_;
}

}  // namespace f2fs

#endif  // __Fuchsia__
