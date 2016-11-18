// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_VMO_FILE_H_
#define LIB_MTL_VMO_FILE_H_

#include "lib/ftl/files/unique_fd.h"
#include "mx/vmo.h"

#include <string>

namespace mtl {

// Make a new shared buffer with the contents of a file.
bool VmoFromFd(ftl::UniqueFD fd, mx::vmo* handle_ptr);

// Make a new shared buffer with the contents of a file.
bool VmoFromFilename(const std::string& filename, mx::vmo* handle_ptr);

}  // namespace mtl

#endif  // LIB_MTL_VMO_FILE_H_
