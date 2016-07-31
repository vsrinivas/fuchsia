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

}  // namespace files

#endif  // LIB_FTL_FILES_PATH_H_
