// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <stdio.h>
#include <zircon/status.h>

#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

int main(int argc, const char** argv) {
  if (const zx_status_t status = StdoutToDebuglog::Init(); status != ZX_OK) {
    fprintf(stderr, "sysinfo: StdoutToDebuglog::Init() = %s\n", zx_status_get_string(status));
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  if (const zx::result status = outgoing.AddProtocol<fuchsia_sysinfo::SysInfo>(
          [](fidl::ServerEnd<fuchsia_sysinfo::SysInfo> server_end) {
            constexpr char kSysInfoPath[] = "/dev/sys/platform";

            if (const zx::result status = component::Connect<fuchsia_sysinfo::SysInfo>(
                    std::move(server_end), kSysInfoPath);
                status.is_error()) {
              fprintf(stderr, "sysinfo: component::Connect(\"%s\") = %s\n", kSysInfoPath,
                      status.status_string());
            }
          });
      status.is_error()) {
    fprintf(stderr, "sysinfo: outgoing.AddProtocol() = %s\n", status.status_string());
    return -1;
  }

  if (const zx::result status = outgoing.ServeFromStartupInfo(); status.is_error()) {
    fprintf(stderr, "sysinfo: outgoing.ServeFromStartupInfo() = %s\n", status.status_string());
    return -1;
  }

  if (const zx_status_t status = loop.Run(); status != ZX_OK) {
    fprintf(stderr, "sysinfo: loop.Run() = %s\n", zx_status_get_string(status));
    return -1;
  }

  return 0;
}
