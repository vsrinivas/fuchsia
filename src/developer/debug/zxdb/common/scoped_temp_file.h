// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_SCOPED_TEMP_FILE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_SCOPED_TEMP_FILE_H_

#include <string>

#include <fbl/unique_fd.h>

namespace zxdb {

// Creates a unique open temp file on construction, closes and deletes it on destruction.
class ScopedTempFile {
 public:
  ScopedTempFile();
  ~ScopedTempFile();

  int fd() const { return fd_.get(); }
  const std::string& name() const { return name_; }

 private:
  std::string name_;
  fbl::unique_fd fd_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_SCOPED_TEMP_FILE_H_
