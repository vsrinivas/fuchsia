// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl/txn_header.h>
#include <lib/zircon-internal/debug.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace fio = fuchsia_io;

namespace fs {

zx_status_t FuchsiaVfs::UnmountHandle(fidl::ClientEnd<fuchsia_io::DirectoryAdmin> handle,
                                      zx::time deadline) {
  fidl::WireResult<fio::DirectoryAdmin::Unmount> result(handle, deadline.get());
  if (!result.ok()) {
    return result.status();
  }
  return result.Unwrap()->s;
}

}  // namespace fs
