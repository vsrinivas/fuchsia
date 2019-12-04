// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_FUCHSIA_PLATFORM_H_
#define SRC_LEDGER_BIN_PLATFORM_FUCHSIA_PLATFORM_H_

#include "src/ledger/bin/platform/platform.h"
#include "util/env_fuchsia.h"

namespace ledger {

class FuchsiaFileSystem : public FileSystem {
 public:
  FuchsiaFileSystem() = default;
  ~FuchsiaFileSystem() = default;

  // Opens a FileDescriptor at the given |path|. If the operation fails, the returned FileDescriptor
  // will be invalid.
  std::unique_ptr<FileDescriptor> OpenFD(DetachedPath path, DetachedPath* result_path);

  // Creates a new LevelDB environment at |db_path|. If an FD is not already opened at the location
  // of |db_path| a new one is opend, and |updated_db_path| reflects this update. Otherwise,
  // |updated_db_path| contains the same path as |db_path|.
  std::unique_ptr<leveldb::Env> MakeLevelDbEnvironment(DetachedPath db_path,
                                                       DetachedPath* updated_db_path) override;

  // FileSystem:
  bool ReadFileToString(DetachedPath path, std::string* content) override;
  bool WriteFile(DetachedPath path, const std::string& content) override;
  bool IsFile(DetachedPath path) override;
  bool GetFileSize(DetachedPath path, uint64_t* size) override;
  bool CreateDirectory(DetachedPath path) override;
  bool IsDirectory(DetachedPath path) override;
  bool DeletePath(DetachedPath path) override;
  bool DeletePathRecursively(DetachedPath path) override;
};

class FuchsiaPlatform : public Platform {
 public:
  FileSystem* file_system() override { return &file_system_; };

 private:
  FuchsiaFileSystem file_system_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_FUCHSIA_PLATFORM_H_
