// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/memfs/setup.h"

#include <lib/sync/completion.h>

#include "src/storage/memfs/memfs.h"
#include "src/storage/memfs/vnode_dir.h"

namespace memfs {

Setup::Setup(std::unique_ptr<Memfs> memfs, zx::channel root)
    : memfs_(std::move(memfs)), root_(std::move(root)) {}

Setup::Setup(Setup&&) = default;

Setup::~Setup() {
  if (memfs_) {
    // Need to synchronize on teardown.
    sync_completion_t unmounted;
    AsyncTearDown([&unmounted](zx_status_t) { sync_completion_signal(&unmounted); });
    sync_completion_wait(&unmounted, zx::duration::infinite().get());
  }
}

zx_status_t Setup::MountAt(const char* path) {
  if (!root_)
    return ZX_ERR_BAD_STATE;
  if (!path || !path[0])
    return ZX_ERR_NOT_SUPPORTED;

  if (zx_status_t status = fdio_ns_get_installed(&namespace_); status != ZX_OK)
    return status;

  mounted_path_ = path;
  return fdio_ns_bind(namespace_, path, root_.release());
}

void Setup::AsyncTearDown(fit::callback<void(zx_status_t)> cb) {
  ZX_DEBUG_ASSERT(memfs_);

  if (!mounted_path_.empty()) {
    // If unmounting fails we continue with tear-down since there's not much else to do.
    fdio_ns_unbind(namespace_, mounted_path_.c_str());
  }

  Memfs* memfs_ptr = memfs_.get();  // Need to both use & move in below line.
  memfs_ptr->Shutdown([memfs = std::move(memfs_), cb = std::move(cb)](zx_status_t status) mutable {
    memfs.reset();  // Release memfs class class before signaling.
    cb(status);
  });
}

void Setup::ForceSyncTearDownUnsafe() {
  ZX_DEBUG_ASSERT(memfs_);

  if (!mounted_path_.empty()) {
    // If unmounting fails we continue with tear-down since there's not much else to do.
    fdio_ns_unbind(namespace_, mounted_path_.c_str());
  }

  memfs_.reset();
}

zx::status<Setup> Setup::Create(async_dispatcher_t* dispatcher) {
  auto fs_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (fs_endpoints.is_error())
    return fs_endpoints.take_error();

  std::unique_ptr<memfs::Memfs> memfs;
  fbl::RefPtr<memfs::VnodeDir> root;
  if (zx_status_t status = memfs::Memfs::Create(dispatcher, "<tmp>", &memfs, &root);
      status != ZX_OK) {
    return zx::error(status);
  }

  if (zx_status_t status = memfs->ServeDirectory(std::move(root), std::move(fs_endpoints->server));
      status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(Setup(std::move(memfs), fs_endpoints->client.TakeChannel()));
}

}  // namespace memfs
