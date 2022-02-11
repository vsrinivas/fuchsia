// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/svc/outgoing.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"

// Serve /pkg as /pkg in the outgoing directory.
int main(int argc, const char* const* argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (!endpoints.is_ok()) {
    fprintf(stderr, "Couldn't create endpoints, %d\n", endpoints.error_value());
    return -1;
  }
  zx_status_t status;
  status = fdio_open("/pkg",
                     fuchsia_io::wire::kOpenRightReadable | fuchsia_io::wire::kOpenRightExecutable,
                     endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to open package");
    return -1;
  }

  // sys::OutgoingDirectory doesn't support executable rights, so use svc::Outgoing.
  svc::Outgoing outgoing(loop.dispatcher());

  auto pkg_outgoing_dir = fbl::MakeRefCounted<fs::RemoteDir>(std::move(endpoints->client));
  outgoing.root_dir()->AddEntry("pkg", pkg_outgoing_dir);

  status = outgoing.ServeFromStartupInfo();

  if (status != ZX_OK) {
    fprintf(stderr, "Failed to serve outgoing.");
    return -1;
  }

  loop.Run();
  return 0;
}
