// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dump-file.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <cstdio>
#include <limits>
#include <tuple>

#include "dump-file-mmap.h"
#include "dump-file-stdio.h"

namespace zxdump::internal {

DumpFile::~DumpFile() = default;

// Map the file in if possible, falling back to reading via stdio.
fit::result<Error, std::unique_ptr<DumpFile>> DumpFile::Open(fbl::unique_fd fd, bool try_mmap) {
  struct stat st;
  if (fstat(fd.get(), &st) < 0) {
    return fit::error(Error{"fstat", ZX_ERR_IO});
  }
  const size_t size = static_cast<size_t>(st.st_size);

  if (try_mmap) {
    void* data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    if (data != MAP_FAILED) {
      return fit::ok(std::make_unique<Mmap>(data, size));
    }
  }

  if (FILE* f = fdopen(fd.get(), "rb")) {
    std::ignore = fd.release();  // The FILE took ownership of the fd.
    return fit::ok(std::make_unique<Stdio>(
        f, S_ISREG(st.st_mode) ? size : std::numeric_limits<size_t>::max()));
  }

  return fit::error(Error{"fdopen", ZX_ERR_IO});
}

}  // namespace zxdump::internal
