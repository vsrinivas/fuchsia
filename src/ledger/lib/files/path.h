// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FILES_PATH_H_
#define SRC_LEDGER_LIB_FILES_PATH_H_

#include <string>

namespace ledger {

// Returns the directory name component of the given path.
std::string GetDirectoryName(const std::string& path);

// Returns the basename component of the given path by stripping everything up
// to and including the last slash.
std::string GetBaseName(const std::string& path);

// Delete the file or directory at the given path. If recursive is true, and
// path is a directory, also delete the directory's content. If |path| is
// relative, resolve it with |root_fd| as reference. See |openat(2)|.
bool DeletePathAt(int root_fd, const std::string& path, bool recursive);

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_FILES_PATH_H_
