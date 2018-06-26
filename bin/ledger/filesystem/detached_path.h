// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_FILESYSTEM_DETACHED_PATH_H_
#define PERIDOT_BIN_LEDGER_FILESYSTEM_DETACHED_PATH_H_

#include <fcntl.h>
#include <string>

#include "lib/fxl/strings/string_view.h"

namespace ledger {

// Abstraction for a path rooted at a file descriptor.
//
// This class doesn't take ownership of the file descriptor and it is expected
// that the file descriptor will outlive this class and any sub path created
// from it.
class DetachedPath {
 public:
  // If |path| is absolute, DetachedPath is equivalent to it. If |path| is
  // relative, it is resolved with |root_fd| as reference. See |openat(2)|.
  explicit DetachedPath(int root_fd = AT_FDCWD, std::string path = ".");
  ~DetachedPath();
  DetachedPath(const DetachedPath& other);
  DetachedPath(DetachedPath&& other);
  DetachedPath& operator=(const DetachedPath& other);
  DetachedPath& operator=(DetachedPath&&);

  // The file descriptor to the base directory of this path.
  int root_fd() const { return root_fd_; };
  // The relative path to |root_fd|.
  const std::string& path() const { return path_; };

  // A |DetachedPath| representing the |path| appended to the current path.
  DetachedPath SubPath(fxl::StringView path) const;
  // A |DetachedPath| representing all the |path| in |components| appended to
  // the current path.
  DetachedPath SubPath(std::initializer_list<fxl::StringView> components) const;

 private:
  int root_fd_;
  std::string path_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_FILESYSTEM_DETACHED_PATH_H_
