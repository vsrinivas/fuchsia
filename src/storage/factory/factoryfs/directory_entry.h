// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_ENTRY_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_ENTRY_H_

#include <string>
#include <string_view>

#include "src/storage/factory/factoryfs/format.h"

namespace factoryfs {

class DirectoryEntryManager final {
 public:
  DirectoryEntryManager(const DirectoryEntryManager&) = delete;
  DirectoryEntryManager(DirectoryEntryManager&&) = delete;
  DirectoryEntryManager& operator=(const DirectoryEntryManager&) = delete;
  DirectoryEntryManager& operator=(DirectoryEntryManager&&) = delete;

  static zx_status_t Create(const DirectoryEntry* entry,
                            std::unique_ptr<DirectoryEntryManager>* out);

  // Gets size of file data.
  uint32_t GetDataSize() const;

  // Gets device block number of start of file data.
  uint32_t GetDataStart() const;

  // Gets filename.
  std::string_view GetName() const;

  // Gets filename length.
  uint32_t GetNameLen() const;

 private:
  DirectoryEntryManager(const DirectoryEntry* entry);
  const DirectoryEntry& entry() const;

  std::unique_ptr<uint8_t> buffer_;
};

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_DIRECTORY_ENTRY_H_
