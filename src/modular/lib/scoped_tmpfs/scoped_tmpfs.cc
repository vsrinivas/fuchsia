// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/scoped_tmpfs/scoped_tmpfs.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sync/completion.h>
#include <zircon/processargs.h>

#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/logging.h"

namespace scoped_tmpfs {

namespace {
async_loop_config_t MakeConfig() {
  async_loop_config_t result = kAsyncLoopConfigAttachToCurrentThread;
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
  sync_completion_t unmounted;
  memfs_free_filesystem(memfs_, &unmounted);
  zx_status_t status = sync_completion_wait(&unmounted, ZX_SEC(10));
  FXL_DCHECK(status == ZX_OK) << status;
}

}  // namespace scoped_tmpfs
