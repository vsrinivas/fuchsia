// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/zx/eventpair.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "pty-server.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

int main(int argc, const char** argv) {
  if (zx_status_t status = StdoutToDebuglog::Init(); status != ZX_OK) {
    printf("console: failed to init stdout to debuglog: %s\n", zx_status_get_string(status));
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  if (zx::result status = outgoing.AddProtocol<fuchsia_hardware_pty::Device>(
          [dispatcher =
               loop.dispatcher()](fidl::ServerEnd<fuchsia_hardware_pty::Device> server_end) {
            zx::result args = PtyServer::Args::Create();
            if (args.is_error()) {
              server_end.Close(args.status_value());
              return;
            }
            std::shared_ptr server =
                std::make_shared<PtyServer>(std::move(args.value()), dispatcher);
            server->AddConnection(std::move(server_end));
          });
      status.is_error()) {
    printf("console: outgoing.AddProtocol() = %s\n", status.status_string());
    return status.status_value();
  }

  if (zx::result status = outgoing.ServeFromStartupInfo(); status.is_error()) {
    printf("console: outgoing.ServeFromStartupInfo() = %s\n", status.status_string());
    return status.status_value();
  }

  if (zx_status_t status = loop.Run(); status != ZX_OK) {
    printf("console: lop.Run() = %s\n", zx_status_get_string(status));
    return status;
  }

  return 0;
}
