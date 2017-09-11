// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FILES_PATH_H_
#define LIB_FXL_FILES_PATH_H_

#include <string>

#include "lib/fxl/fxl_export.h"

namespace files {

// Resolves ".." and "." components of the path syntactically without consulting
// the file system.
FXL_EXPORT std::string SimplifyPath(std::string path);

// Returns the absolute path of a possibly relative path.
// It doesn't consult the filesystem or simplify the path.
FXL_EXPORT std::string AbsolutePath(const std::string& path);

// Returns the directory name component of the given path.
FXL_EXPORT std::string GetDirectoryName(const std::string& path);

// Returns the basename component of the given path by stripping everything up
// to and including the last slash.
FXL_EXPORT std::string GetBaseName(const std::string& path);

// Delete the file or directly at the given path. If recursive is true, and path
// is a directory, also delete the directory's content.
FXL_EXPORT bool DeletePath(const std::string& path, bool recursive);

}  // namespace files

#endif  // LIB_FXL_FILES_PATH_H_
