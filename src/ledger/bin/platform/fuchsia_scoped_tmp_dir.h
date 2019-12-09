// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_DIR_H_
#define SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_DIR_H_

#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/platform/scoped_tmp_dir.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace ledger {

class FuchsiaScopedTmpDir : public ScopedTmpDir {
 public:
  // Creates a new FuchsiaScopedTmpDir under the |parent_path|.
  explicit FuchsiaScopedTmpDir(DetachedPath parent_path)
      : scoped_temp_dir_(parent_path.root_fd(), parent_path.path()) {}

  ~FuchsiaScopedTmpDir() = default;

  FuchsiaScopedTmpDir(ScopedTmpDir&&) = delete;
  FuchsiaScopedTmpDir& operator=(ScopedTmpDir&&) = delete;

  virtual DetachedPath path() {
    return DetachedPath(scoped_temp_dir_.root_fd(), scoped_temp_dir_.path());
  }

 private:
  files::ScopedTempDirAt scoped_temp_dir_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_DIR_H_
