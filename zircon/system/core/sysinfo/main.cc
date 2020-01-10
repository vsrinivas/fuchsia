// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl-async/cpp/bind.h>
#include <lib/svc/outgoing.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

#include "sysinfo.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  auto sysinfo = sysinfo::SysInfo();

  svc::Outgoing outgoing(dispatcher);
  zx_status_t status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("console: outgoing.ServeFromStartupInfo() = %s\n", zx_status_get_string(status));
    return -1;
  }
  status = outgoing.svc_dir()->AddEntry(
      ::llcpp::fuchsia::sysinfo::SysInfo::Name,
      fbl::AdoptRef(new fs::Service([dispatcher, sysinfo](zx::channel svc_request) mutable {
        zx_status_t status = fidl::Bind(dispatcher, std::move(svc_request), &sysinfo);
        if (status != ZX_OK) {
          printf("sysinfo: fidl::Bind(_) = %s\n", zx_status_get_string(status));
        }
        return status;
      })));

  status = loop.Run();
  ZX_ASSERT(status == ZX_OK);
  return 0;
}
