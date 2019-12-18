// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FILES_DIRECTORY_H_
#define SRC_LEDGER_LIB_FILES_DIRECTORY_H_

#include <string>
#include <vector>

namespace ledger {

// Returns whether the given path is a directory. If |path| is relative, resolve
// it with |root_fd| as reference. See |openat(2)|.
bool IsDirectoryAt(int root_fd, const std::string& path);

// Create a directory at the given path. If necessary, creates any intermediary
// directory. If |path| is relative, resolve it with |root_fd| as reference. See
// |openat(2)|.
bool CreateDirectoryAt(int root_fd, const std::string& path);

// List the contents of a directory. If returns false, errno will be set. If |path| is relative,
// resolve it with |root_fd| as reference. See |openat(2)|.
bool ReadDirContentsAt(int root_fd, const std::string& path, std::vector<std::string>* out);

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_FILES_DIRECTORY_H_
