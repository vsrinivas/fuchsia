// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>

int main(int argc, char** argv) {
  fprintf(stderr, "memfs starting up\n");
  async::Loop global_loop(&kAsyncLoopConfigAttachToCurrentThread);

  memfs_filesystem_t* memfs_handle = nullptr;
  zx_handle_t memfs_root = ZX_HANDLE_INVALID;
  zx_status_t status =
      memfs_create_filesystem(global_loop.dispatcher(), &memfs_handle, &memfs_root);
  if (status != ZX_OK) {
    fprintf(stderr, "failed to create memfs: %s\n", zx_status_get_string(status));
    return 1;
  }

  fs::ManagedVfs outgoing_vfs = fs::ManagedVfs(global_loop.dispatcher());
  auto outgoing_dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());

  auto svc_dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
  svc_dir->AddEntry("fuchsia.io.Directory",
                    fbl::AdoptRef<fs::RemoteDir>(new fs::RemoteDir(zx::channel(memfs_root))));

  outgoing_dir->AddEntry("svc", std::move(svc_dir));

  outgoing_vfs.ServeDirectory(outgoing_dir,
                              zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));

  fprintf(stderr, "memfs initialization complete\n");
  status = global_loop.Run();
  if (status != ZX_OK) {
    fprintf(stderr, "error running async loop: %s\n", zx_status_get_string(status));
    return 1;
  }
  return 0;
}
