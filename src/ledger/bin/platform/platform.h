// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_PLATFORM_H_
#define SRC_LEDGER_BIN_PLATFORM_PLATFORM_H_

#include <memory>
#include <vector>

#include "leveldb/env.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/platform/scoped_tmp_dir.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"

namespace ledger {

// An abstraction over all file system related operations.
class FileSystem {
 public:
  FileSystem() = default;

  virtual ~FileSystem() = default;

  // Creates and returns a new LevelDB environment at |db_path|. If creation fails nullptr is
  // returned. |updated_db_path| is updated to contain a path equivalent to |db_path|, but with its
  // |root_fd| potentially updated.
  virtual std::unique_ptr<leveldb::Env> MakeLevelDbEnvironment(DetachedPath db_path,
                                                               DetachedPath* updated_db_path) = 0;

  // Files.

  // Reads the file at the given |path| and stores the result in |content|. Returns true on success
  // or false otherwise.
  virtual bool ReadFileToString(DetachedPath path, std::string* content) = 0;

  // Writes the |content| to file on the given |path|. Returns true on success or false otherwise.
  virtual bool WriteFile(DetachedPath path, const std::string& content) = 0;

  // Returns whether the given |path| refers to a file.
  virtual bool IsFile(DetachedPath path) = 0;

  // Updates |size| with the size of the file at the given |path|. Returns true on success and false
  // on failure.
  virtual bool GetFileSize(DetachedPath path, uint64_t* size) = 0;

  // Directories.

  // Creates a directory at the given path. If necessary, creates any intermediary directory.
  // Returns true on success or false otherwise.
  virtual bool CreateDirectory(DetachedPath path) = 0;

  // Returns whether the given |path| refers to a directory.
  virtual bool IsDirectory(DetachedPath path) = 0;

  // Lists the contents of the directory at the given |path| and stores them in |dir_contents|. The
  // current path (e.g. ".") and the parent path (e.g. "..") are not included in the result. Returns
  // true on success or false otherwise.
  virtual bool GetDirectoryContents(DetachedPath path, std::vector<std::string>* dir_contents) = 0;

  // Creates a new ScopedTmpDir under the given |parent_path|.
  virtual std::unique_ptr<ScopedTmpDir> CreateScopedTmpDir(DetachedPath parent_path) = 0;

  // Creates a new ScopedTmpFs.
  virtual std::unique_ptr<ScopedTmpLocation> CreateScopedTmpLocation() = 0;

  // Paths.

  // Deletes the file or empty directory at the given |path|. If the |path| refers to a non-empty
  // directory, the operation fails. Returns true on success or false otherwise.
  virtual bool DeletePath(DetachedPath path) = 0;

  // Deletes the file or directory at the given |path|. If the |path| refers to a directory, all its
  // contents are recursively deleted. Returns true on success or false otherwise.
  virtual bool DeletePathRecursively(DetachedPath path) = 0;

  // Renames the |origin| path to |destination|. Returns true on success or false otherwise.
  virtual bool Rename(DetachedPath origin, DetachedPath destination) = 0;
};

// Provides all platform specific operations.
class Platform {
 public:
  Platform() = default;

  virtual ~Platform() = default;

  virtual FileSystem* file_system() = 0;
};

// Returns the default Platform based on the current operating system.
std::unique_ptr<Platform> MakePlatform();

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_PLATFORM_H_
