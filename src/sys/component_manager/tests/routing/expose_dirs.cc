// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/channel.h>

#include <fs/remote-dir.h>

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

  fidl::InterfaceHandle<fuchsia::io::Node> ro_dir, rw_dir, rx_dir;
  memfs_dir->Clone(fuchsia::io::OPEN_RIGHT_READABLE, ro_dir.NewRequest());
  memfs_dir->Clone(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                   rw_dir.NewRequest());
  memfs_dir->Clone(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE,
                   rx_dir.NewRequest());

  // TODO(fxb/37773): We can't use sys::ComponentContext/vfs::PseudoDir/vfs::RemoteDir here because
  // of a bug in how they handle OPEN_FLAG_POSIX.
  svc::Outgoing outgoing(loop.dispatcher());
  outgoing.root_dir()->AddEntry("read_only",
                                fbl::MakeRefCounted<fs::RemoteDir>(ro_dir.TakeChannel()));
  outgoing.root_dir()->AddEntry("read_write",
                                fbl::MakeRefCounted<fs::RemoteDir>(rw_dir.TakeChannel()));
  outgoing.root_dir()->AddEntry("read_exec",
                                fbl::MakeRefCounted<fs::RemoteDir>(rx_dir.TakeChannel()));
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to serve outgoing dir: %d\n", status);
    return -1;
  }

  loop.Run();
  memfs_free_filesystem(vfs, nullptr);
  return 0;
}
