// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FILES_SCOPED_TEMP_DIR_H_
#define LIB_FXL_FILES_SCOPED_TEMP_DIR_H_

#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace files {

// An object representing a temporary / scratch directory that should be cleaned
// up (recursively) when this object goes out of scope. Note that since
// deletion occurs during the destructor, no further error handling is possible
// if the directory fails to be deleted. As a result, deletion is not
// guaranteed by this class.
//
// The temporary directory is created in |parent_path| relative to |root_fd|. If
// |root_fd| is AT_FDCWD, |parent_path| is relative to the current working
// directory. If |parent_path| is not given, the temporary directory is directly
// created in |root_fd|.
class FXL_EXPORT ScopedTempDirAt {
 public:
  explicit ScopedTempDirAt(int root_fd);
  explicit ScopedTempDirAt(int root_fd, fxl::StringView parent_path);
  ScopedTempDirAt(ScopedTempDirAt&&) = delete;
  ScopedTempDirAt& operator=(ScopedTempDirAt&&) = delete;
  ~ScopedTempDirAt();

  const std::string& path();
  int root_fd();

  bool NewTempFile(std::string* output);
  bool NewTempFileWithData(const std::string& data, std::string* output);

 private:
  int root_fd_;
  std::string directory_path_;
};

// As |ScopedTempDirAt|, but instead of creating the temporary directory
// relative to a file descriptor, it is created either in |parent_path|, or in
// the global temporary directory.
class FXL_EXPORT ScopedTempDir {
 public:
  ScopedTempDir();
  explicit ScopedTempDir(fxl::StringView parent_path);
  ScopedTempDir(ScopedTempDir&&) = delete;
  ScopedTempDir& operator=(ScopedTempDir&&) = delete;
  ~ScopedTempDir();

  const std::string& path();

  bool NewTempFile(std::string* output);
  bool NewTempFileWithData(const std::string& data, std::string* output);

 private:
  ScopedTempDirAt base_;
};

}  // namespace files

#endif  // LIB_FXL_FILES_SCOPED_TEMP_DIR_H_
