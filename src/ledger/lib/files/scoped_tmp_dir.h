// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FILES_SCOPED_TMP_DIR_H_
#define SRC_LEDGER_LIB_FILES_SCOPED_TMP_DIR_H_

#include "src/ledger/lib/files/detached_path.h"

namespace ledger {

// A temporary directory that is cleared (recursively) during destruction.
class ScopedTmpDir {
 public:
  // Creates a temporary directory under the global temporary dir.
  ScopedTmpDir();

  // Creates a temporary directory under |parent_path|.
  ScopedTmpDir(DetachedPath parent_path);
  ~ScopedTmpDir();

  ScopedTmpDir(ScopedTmpDir&&) = delete;
  ScopedTmpDir& operator=(ScopedTmpDir&&) = delete;

  DetachedPath path() { return path_; }

 private:
  DetachedPath path_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_FILES_SCOPED_TMP_DIR_H_
