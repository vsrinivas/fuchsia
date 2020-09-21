// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_TEST_INTEGRATION_UTILS_H_
#define SRC_STORAGE_MINFS_TEST_INTEGRATION_UTILS_H_

#include <string>
#include <string_view>

#include <fbl/unique_fd.h>

// Builds a path string from the root of the filesystem. |name| should be an absolute
// path ("/foo/bar").
std::string BuildPath(const std::string_view& name);

// Creates a directory with the given name (not recursive).
bool CreateDirectory(const std::string_view& name);

// Creates a file with the given name.
fbl::unique_fd CreateFile(const std::string_view& name);

// Opens an existing file.
fbl::unique_fd OpenFile(const std::string_view& name, bool read_only = false);
inline fbl::unique_fd OpenReadOnly(const std::string_view& name) { return OpenFile(name, true); }

#endif  // SRC_STORAGE_MINFS_TEST_INTEGRATION_UTILS_H_
