// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <zircon/device/vfs.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {
void ServeFromNamespace(fs::PseudoDir* out_dir, const char* ns_path, const char* out_path) {
  zx_status_t status;
  zx::channel ns_server, ns_client;
  status = zx::channel::create(0, &ns_server, &ns_client);
  FX_CHECK(status == ZX_OK) << "failed to create channel: " << zx_status_get_string(status);
  status = fdio_open(ns_path, ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DIRECTORY | ZX_FS_RIGHT_WRITABLE,
                     ns_server.release());
  FX_CHECK(status == ZX_OK) << "failed to open " << ns_path << ": " << zx_status_get_string(status);

  auto subdir = fbl::MakeRefCounted<fs::RemoteDir>(
      fidl::ClientEnd<fuchsia_io::Directory>(std::move(ns_client)));
  out_dir->AddEntry(out_path, subdir);
}
}  // namespace

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fs::SynchronousVfs out_vfs(loop.dispatcher());
  fbl::RefPtr<fs::PseudoDir> out_dir(fbl::MakeRefCounted<fs::PseudoDir>());
  ServeFromNamespace(out_dir.get(), "/svc", "svc_for_sys");
  ServeFromNamespace(out_dir.get(), "/svc_from_sys", "svc");

  auto pa_directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  zx_status_t status = out_vfs.ServeDirectory(
      out_dir, fidl::ServerEnd<fuchsia_io::Directory>(zx::channel(pa_directory_request)));
  FX_CHECK(status == ZX_OK) << "failed to serve outgoing dir: " << zx_status_get_string(status);

  loop.Run();
  return 0;
}
