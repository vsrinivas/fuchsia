// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/elfldltl/mapped-fd-file.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/assert.h>

namespace elfldltl {

fit::result<int> MappedFdFile::Init(int fd) {
  // Stat the file to get its size.
  struct stat st;
  if (fstat(fd, &st) < 0) {
    return fit::error{errno};
  }

  // If it's not a regular file, st_size doesn't mean something useful.
  if (!S_ISREG(st.st_mode)) {
    return fit::error{ENOTSUP};
  }

  const size_t file_size = static_cast<size_t>(st.st_size);
  if (st.st_size > static_cast<ssize_t>(file_size)) {
    return fit::error{EFBIG};
  }

  void* mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mapped == MAP_FAILED) {
    return fit::error{errno};
  }

  set_image({static_cast<std::byte*>(mapped), file_size});

  return fit::ok();
}

MappedFdFile::~MappedFdFile() {
  if (!image().empty() && munmap(image().data(), image().size_bytes()) < 0) {
    ZX_DEBUG_ASSERT_MSG(false, "munmap(%p, %#zx): %s", image().data(), image().size_bytes(),
                        strerror(errno));
  }
}

}  // namespace elfldltl
