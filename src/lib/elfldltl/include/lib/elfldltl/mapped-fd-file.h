// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MAPPED_FD_FILE_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MAPPED_FD_FILE_H_

#include <lib/fitx/result.h>

#include "memory.h"

namespace elfldltl {

// elfldltl::MappedFdFile provides the File and Memory APIs and most other
// features of elfldltl::DirectMemory (see <lib/elfldltl/memory.h>), but on a
// read-only mmap'd file's entire contents.
//
// The object is default-constructible and move-only.  The Init() function uses
// an unowned fd to set up the mapping but does not need the fd thereafter.
// The mapping will be removed on the object's destruction.
class MappedFdFile : public DirectMemory {
 public:
  MappedFdFile() = default;

  MappedFdFile(const MappedFdFile&) = delete;

  MappedFdFile(MappedFdFile&& other) noexcept : DirectMemory(other.image(), other.base()) {
    other.set_image({});
  }

  MappedFdFile& operator=(MappedFdFile&& other) noexcept {
    auto old_image = image();
    set_image(other.image());
    other.set_image(old_image);
    set_base(other.base());
    return *this;
  }

  // Fails with an errno code if fstat or mmap failed.
  fitx::result<int> Init(int fd);

  ~MappedFdFile();

 private:
  // Make this private so it can't be used.
  using DirectMemory::set_image;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MAPPED_FD_FILE_H_
