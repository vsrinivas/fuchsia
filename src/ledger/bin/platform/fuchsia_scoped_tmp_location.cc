// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/platform/fuchsia_scoped_tmp_location.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sync/completion.h>
#include <zircon/processargs.h>

#include "src/ledger/bin/platform/fd.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {

namespace {
async_loop_config_t MakeConfig() {
  async_loop_config_t result = kAsyncLoopConfigAttachToCurrentThread;
  result.make_default_for_current_thread = false;
  return result;
}
}  // namespace

FuchsiaScopedTmpLocation::FuchsiaScopedTmpLocation() : config_(MakeConfig()), loop_(&config_) {
  zx_status_t status = loop_.StartThread("tmp_location_thread");
  LEDGER_CHECK(status == ZX_OK);
  zx_handle_t root_handle;
  status = memfs_create_filesystem(loop_.dispatcher(), &memfs_, &root_handle);
  LEDGER_CHECK(status == ZX_OK);
  root_fd_ = OpenChannelAsFileDescriptor(zx::channel(root_handle));
  LEDGER_CHECK(root_fd_.is_valid());
}

FuchsiaScopedTmpLocation::~FuchsiaScopedTmpLocation() {
  root_fd_.reset();
  sync_completion_t unmounted;
  memfs_free_filesystem(memfs_, &unmounted);
  zx_status_t status = sync_completion_wait(&unmounted, ZX_SEC(10));
  LEDGER_CHECK(status == ZX_OK) << status;
}
}  // namespace ledger
