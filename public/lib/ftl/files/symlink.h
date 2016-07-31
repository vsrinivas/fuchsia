// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FILES_SYMLINK_H_
#define LIB_FTL_FILES_SYMLINK_H_

#include <string>

namespace files {

// If |path| is a symbolic link, this function will return true and set
// |resolved_path| to the path pointed to by the symbolic link. Otherwise,
// this function will return false and |resolved_path| will be the empty string.
bool ReadSymbolicLink(const std::string& path, std::string* resolved_path);

// Returns the real path for the given path by unwinding symbolic links and
// directory traversals.
std::string GetAbsoluteFilePath(const std::string& path);

}  // namespace files

#endif  // LIB_FTL_FILES_SYMLINK_H_
