// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/namespace.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <zircon/assert.h>

#include <memory>

#include "src/storage/memfs/setup.h"

struct memfs_filesystem {
  memfs::Setup setup;

  explicit memfs_filesystem(memfs::Setup&& s) : setup(std::move(s)) {}
};

zx_status_t memfs_create_filesystem(async_dispatcher_t* dispatcher, memfs_filesystem_t** out_fs,
                                    zx_handle_t* out_root) {
  ZX_DEBUG_ASSERT(dispatcher != nullptr);
  ZX_DEBUG_ASSERT(out_fs != nullptr);
  ZX_DEBUG_ASSERT(out_root != nullptr);

  zx::status<memfs::Setup> setup_or = memfs::Setup::Create(dispatcher);
  if (setup_or.is_error())
    return setup_or.error_value();

  *out_root = setup_or->root().release();
  *out_fs = new memfs_filesystem_t(std::move(*setup_or));
  return ZX_OK;
}

zx_status_t memfs_install_at(async_dispatcher_t* dispatcher, const char* path,
                             memfs_filesystem_t** out_fs) {
  ZX_DEBUG_ASSERT(dispatcher);
  ZX_DEBUG_ASSERT(path);
  ZX_DEBUG_ASSERT(out_fs);

  zx::status<memfs::Setup> setup_or = memfs::Setup::Create(dispatcher);
  if (setup_or.is_error())
    return setup_or.error_value();

  if (zx_status_t status = setup_or->MountAt(path); status != ZX_OK)
    return status;

  *out_fs = new memfs_filesystem_t(std::move(*setup_or));
  return ZX_OK;
}

zx_status_t memfs_uninstall_unsafe(memfs_filesystem_t* fs, const char* path) {
  ZX_DEBUG_ASSERT(fs);

  fs->setup.ForceSyncTearDownUnsafe();

  delete fs;
  return ZX_OK;
}

void memfs_free_filesystem(memfs_filesystem_t* fs, sync_completion_t* unmounted) {
  ZX_DEBUG_ASSERT(fs);

  // Note: This deletes the memfs_filesystem_t pointer on the memfs thread which might be different
  // than the current one.
  fs->setup.AsyncTearDown([fs, unmounted](zx_status_t status) {
    delete fs;
    if (unmounted) {
      sync_completion_signal(unmounted);
    }
  });
}
