// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_TEST_FILE_H_
#define SRC_LIB_ZXDUMP_TEST_FILE_H_

#include <cstdio>

#include <fbl/unique_fd.h>

namespace zxdump::testing {

// This maintains an anonymous temporary file that can be written and read
// back.  It's automatically cleaned up.
class TestFile {
 public:
  // Get a freshly-dup'd file descriptor to the file, rewound to the beginning.
  // It can be used to either read or write the file (and might support mmap).
  fbl::unique_fd RewoundFd() {
    rewind(tmpfile_);
    return fbl::unique_fd{dup(fileno(tmpfile_))};
  }

  FILE* stdio() { return tmpfile_; }

  ~TestFile() {
    if (tmpfile_) {
      fclose(tmpfile_);
    }
  }

 private:
  FILE* tmpfile_ = tmpfile();
};

}  // namespace zxdump::testing

#endif  // SRC_LIB_ZXDUMP_TEST_FILE_H_
