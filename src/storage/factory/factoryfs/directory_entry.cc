// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/directory_entry.h"

#include "src/storage/factory/factoryfs/factoryfs.h"

namespace factoryfs {

DirectoryEntryManager::DirectoryEntryManager(const DirectoryEntry* directory_entry) {
  buffer_ = std::make_unique<uint32_t[]>(DirentSize(directory_entry->name_len) / sizeof(uint32_t));
  memcpy(buffer_.get(), directory_entry, DirentSize(directory_entry->name_len));
}

zx_status_t DirectoryEntryManager::Create(const DirectoryEntry* directory_entry,
                                          std::unique_ptr<DirectoryEntryManager>* out) {
  if (!directory_entry || out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (directory_entry->name_len == 0 || directory_entry->name_len > kFactoryfsMaxNameSize) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  *out = std::unique_ptr<DirectoryEntryManager>(new DirectoryEntryManager(directory_entry));
  return ZX_OK;
}

const DirectoryEntry& DirectoryEntryManager::entry() const {
  return *reinterpret_cast<DirectoryEntry*>(buffer_.get());
}

uint32_t DirectoryEntryManager::GetDataSize() const { return entry().data_len; }

uint32_t DirectoryEntryManager::GetNameLen() const { return entry().name_len; }

std::string_view DirectoryEntryManager::GetName() const {
  return std::string_view(entry().name, entry().name_len);
}

uint32_t DirectoryEntryManager::GetDataStart() const { return entry().data_off; }

}  // namespace factoryfs
