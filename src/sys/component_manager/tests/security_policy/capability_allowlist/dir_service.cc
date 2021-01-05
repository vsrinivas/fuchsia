// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/channel.h>

#include <fs/remote_dir.h>

int main(int argc, char* argv[]) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  async::Loop memfs_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (memfs_loop.StartThread() != ZX_OK) {
    fprintf(stderr, "Failed to start memfs loop\n");
    return -1;
  }

  memfs_filesystem_t* vfs;
  zx::channel memfs_channel;
  zx_status_t status =
      memfs_create_filesystem(memfs_loop.dispatcher(), &vfs, memfs_channel.reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to create memfs: %d\n", status);
    return -1;
  }

  fidl::InterfacePtr<fuchsia::io::Directory> memfs_dir;
  memfs_dir.Bind(std::move(memfs_channel));

  fidl::InterfaceHandle<fuchsia::io::Node> restricted_dir;
  fidl::InterfaceHandle<fuchsia::io::Node> unrestricted_dir;
  svc::Outgoing outgoing(loop.dispatcher());
  memfs_dir->Clone(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                   restricted_dir.NewRequest());
  memfs_dir->Clone(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                   unrestricted_dir.NewRequest());
  outgoing.root_dir()->AddEntry("restricted",
                                fbl::MakeRefCounted<fs::RemoteDir>(restricted_dir.TakeChannel()));
  outgoing.root_dir()->AddEntry("unrestricted",
                                fbl::MakeRefCounted<fs::RemoteDir>(unrestricted_dir.TakeChannel()));
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to serve outgoing dir: %d\n", status);
    return -1;
  }

  loop.Run();
  memfs_free_filesystem(vfs, nullptr);
  return 0;
}
