// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FILES_PATH_H_
#define LIB_FTL_FILES_PATH_H_

#include <string>

namespace files {

// Resolves ".." and "." components of the path syntactically without consulting
// the file system.
std::string SimplifyPath(std::string path);

// Returns the directory name component of the given path.
std::string GetDirectoryName(std::string path);

// Delete the file or directly at the given path. If recursive is true, and path
// is a directory, also delete the directory's content.
bool DeletePath(const std::string& path, bool recursive);

}  // namespace files

#endif  // LIB_FTL_FILES_PATH_H_
