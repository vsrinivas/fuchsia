// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_COPIER_H_
#define SRC_STORAGE_FSHOST_COPIER_H_

#include <lib/zx/status.h>

#include <filesystem>
#include <list>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <fbl/unique_fd.h>

namespace fshost {

class Copier {
 public:
  struct File {
    std::string name;
    std::string contents;
  };
  struct Directory;
  using DirectoryEntry = std::variant<File, Directory>;
  using DirectoryEntries = std::list<DirectoryEntry>;
  struct Directory {
    std::string name;
    DirectoryEntries entries;
  };

  // Reads all the data at |root_fd| except for the files and directories that match
  // |excluded_paths|.
  static zx::result<Copier> Read(fbl::unique_fd root_fd,
                                 const std::vector<std::filesystem::path>& excluded_paths = {});

  Copier() = default;
  Copier(Copier&&) = default;
  Copier& operator=(Copier&&) = default;

  // Writes all data to the given root fd.
  zx_status_t Write(fbl::unique_fd root_fd) const;

  // Inserts a file into the in-memory structure creating parent directories as necessary.
  // Returns an error if the file already exists or a directory could not be created because a file
  // with the same name already exists.
  zx::result<> InsertFile(const std::filesystem::path& path, std::string contents);

  const DirectoryEntries& entries() const { return entries_; }

  bool empty() const { return entries_.empty(); }

 private:
  DirectoryEntries entries_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_COPIER_H_
