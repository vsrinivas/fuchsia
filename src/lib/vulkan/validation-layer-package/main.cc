// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

// Serve /pkg as the outgoing directory.
int main(int argc, const char* const* argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fidl::InterfaceHandle<fuchsia::io::Directory> pkg_dir;
  zx_status_t status;
  status = fdio_open("/pkg", fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE,
                     pkg_dir.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to open package");
    return -1;
  }

  // Use fs:: instead of vfs:: because vfs doesn't support executable directories.
  fs::SynchronousVfs vfs(loop.dispatcher());
  auto root = fbl::MakeRefCounted<fs::PseudoDir>();

  auto remote = fbl::MakeRefCounted<fs::RemoteDir>(
      fidl::ClientEnd<fuchsia_io::Directory>(pkg_dir.TakeChannel()));
  root->AddEntry("pkg", remote);

  zx::channel dir_request = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  auto options = fs::VnodeConnectionOptions::ReadExec();
  status = vfs.Serve(root, fidl::ServerEnd<fuchsia_io::Node>(std::move(dir_request)), options);

  if (status != ZX_OK) {
    fprintf(stderr, "Failed to serve outgoing.");
    return -1;
  }

  loop.Run();
  return 0;
}
