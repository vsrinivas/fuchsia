// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_SCOPED_TMP_DIR_H_
#define SRC_LEDGER_BIN_PLATFORM_SCOPED_TMP_DIR_H_

namespace ledger {

// A temporary directory that is cleared (recursively) during destruction.
class ScopedTmpDir {
 public:
  ScopedTmpDir() = default;
  virtual ~ScopedTmpDir() = default;

  ScopedTmpDir(ScopedTmpDir&&) = delete;
  ScopedTmpDir& operator=(ScopedTmpDir&&) = delete;

  virtual DetachedPath path() = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_SCOPED_TMP_DIR_H_
