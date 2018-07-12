// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

#include <lib/fdio/util.h>
#include <lib/fsl/io/fd.h>
#include <lib/fxl/logging.h>
#include <sync/completion.h>
#include <zircon/processargs.h>

namespace scoped_tmpfs {

namespace {
async_loop_config_t MakeConfig() {
  async_loop_config_t result = kAsyncLoopConfigMakeDefault;
  result.make_default_for_current_thread = false;
  return result;
}
}  // namespace

ScopedTmpFS::ScopedTmpFS() : config_(MakeConfig()), loop_(&config_) {
  zx_status_t status = loop_.StartThread("tmpfs_thread");
  FXL_CHECK(status == ZX_OK);
  zx_handle_t root_handle;
  status = memfs_create_filesystem(loop_.dispatcher(), &memfs_, &root_handle);
  FXL_CHECK(status == ZX_OK);
  root_fd_ = fsl::OpenChannelAsFileDescriptor(zx::channel(root_handle));
  FXL_CHECK(root_fd_.is_valid());
}

ScopedTmpFS::~ScopedTmpFS() {
  root_fd_.reset();
  completion_t unmounted;
  memfs_free_filesystem(memfs_, &unmounted);
  zx_status_t status = completion_wait(&unmounted, ZX_SEC(3));
  FXL_DCHECK(status == ZX_OK);
}

}  // namespace scoped_tmpfs
