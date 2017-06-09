// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/vmo/file.h"

#include <fcntl.h>
#include <mxio/io.h>
#include <unistd.h>

#include "lib/ftl/logging.h"

namespace mtl {

bool VmoFromFd(ftl::UniqueFD fd, mx::vmo* handle_ptr) {
  FTL_CHECK(handle_ptr);

  mx_handle_t result = MX_HANDLE_INVALID;
  mx_status_t status = mxio_get_vmo(fd.get(), &result);
  if (status != MX_OK)
    return false;
  handle_ptr->reset(result);
  return true;
}

bool VmoFromFilename(const std::string& filename, mx::vmo* handle_ptr) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd == -1) {
    FTL_LOG(WARNING) << "mx::vmo::open failed to open file " << filename;
    return false;
  }
  return VmoFromFd(ftl::UniqueFD(fd), handle_ptr);
}

}  // namespace mtl
