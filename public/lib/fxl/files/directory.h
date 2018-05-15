// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FILES_DIRECTORY_H_
#define LIB_FXL_FILES_DIRECTORY_H_

#include <string>

#include "lib/fxl/fxl_export.h"

namespace files {

// Returns the current directory. If the current directory cannot be determined,
// this function will terminate the process.
FXL_EXPORT std::string GetCurrentDirectory();

// Returns whether the given path is a directory.
FXL_EXPORT bool IsDirectory(const std::string& path);

// Returns whether the given path is a directory. If |path| is relative, resolve
// it with |root_fd| as reference. See |openat(2)|.
FXL_EXPORT bool IsDirectoryAt(int root_fd, const std::string& path);

// Create a directory at the given path. If necessary, creates any intermediary
// directory.
FXL_EXPORT bool CreateDirectory(const std::string& path);

// Create a directory at the given path. If necessary, creates any intermediary
// directory. If |path| is relative, resolve it with |root_fd| as reference. See
// |openat(2)|.
FXL_EXPORT bool CreateDirectoryAt(int root_fd, const std::string& path);

}  // namespace files

#endif  // LIB_FXL_FILES_DIRECTORY_H_
