// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/channel.h>

#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/storage/memfs/scoped_memfs.h"

int main(int argc, char* argv[]) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  async::Loop memfs_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (memfs_loop.StartThread() != ZX_OK) {
    fprintf(stderr, "Failed to start memfs loop\n");
    return -1;
  }

  zx::result<ScopedMemfs> memfs = ScopedMemfs::Create(memfs_loop.dispatcher());
  if (memfs.is_error()) {
    fprintf(stderr, "Failed to create memfs: %s\n", memfs.status_string());
    return -1;
  }

  fidl::InterfacePtr<fuchsia::io::Directory> memfs_dir;
  memfs_dir.Bind(memfs->root().TakeChannel());

  fidl::InterfaceHandle<fuchsia::io::Node> ro_dir;
  fidl::InterfaceHandle<fuchsia::io::Node> rw_dir;
  fidl::InterfaceHandle<fuchsia::io::Node> rx_dir;
  fidl::InterfaceHandle<fuchsia::io::Node> ra_dir;
  fidl::InterfaceHandle<fuchsia::io::Node> r_after_scoped_dir;

  memfs_dir->Clone(fuchsia::io::OpenFlags::RIGHT_READABLE, ro_dir.NewRequest());
  memfs_dir->Clone(fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                   rw_dir.NewRequest());
  memfs_dir->Clone(
      fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_EXECUTABLE,
      rx_dir.NewRequest());
  memfs_dir->Clone(fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                   r_after_scoped_dir.NewRequest());

  svc::Outgoing outgoing(loop.dispatcher());
  outgoing.root_dir()->AddEntry("read_only",
                                fbl::MakeRefCounted<fs::RemoteDir>(ro_dir.TakeChannel()));
  outgoing.root_dir()->AddEntry("read_write",
                                fbl::MakeRefCounted<fs::RemoteDir>(rw_dir.TakeChannel()));
  outgoing.root_dir()->AddEntry("read_exec",
                                fbl::MakeRefCounted<fs::RemoteDir>(rx_dir.TakeChannel()));
  outgoing.root_dir()->AddEntry("read_only_after_scoped", fbl::MakeRefCounted<fs::RemoteDir>(
                                                              r_after_scoped_dir.TakeChannel()));
  zx_status_t status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to serve outgoing dir: %d\n", status);
    return -1;
  }

  loop.Run();
  return 0;
}
