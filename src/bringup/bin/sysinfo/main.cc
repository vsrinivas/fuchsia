// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl-async/cpp/bind.h>
#include <lib/svc/outgoing.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "sysinfo.h"

int main(int argc, const char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    return status;
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  auto sysinfo = sysinfo::SysInfo();

  svc::Outgoing outgoing(dispatcher);
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("console: outgoing.ServeFromStartupInfo() = %s\n", zx_status_get_string(status));
    return -1;
  }
  status = outgoing.svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_sysinfo::SysInfo>,
      fbl::MakeRefCounted<fs::Service>([dispatcher, sysinfo](zx::channel svc_request) mutable {
        zx_status_t status =
            fidl::BindSingleInFlightOnly(dispatcher, std::move(svc_request), &sysinfo);
        if (status != ZX_OK) {
          printf("sysinfo: fidl::BindSingleInFlightOnly(_) = %s\n", zx_status_get_string(status));
        }
        return status;
      }));

  status = loop.Run();
  ZX_ASSERT(status == ZX_OK);
  return 0;
}
