// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fs-pty/service.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/resource.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "console.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace {

zx::resource GetRootResource() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return {};
  }
  status = fdio_service_connect("/svc/fuchsia.boot.RootResource", remote.release());
  if (status != ZX_OK) {
    printf("console: Could not connect to RootResource service: %s\n",
           zx_status_get_string(status));
    return {};
  }

  ::llcpp::fuchsia::boot::RootResource::SyncClient client(std::move(local));
  auto result = client.Get();
  if (result.status() != ZX_OK) {
    printf("console: Could not retrieve RootResource: %s\n", zx_status_get_string(result.status()));
    return {};
  }
  zx::resource root_resource(std::move(result.Unwrap()->resource));
  return root_resource;
}

}  // namespace

int main(int argc, const char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    return status;
  }

  zx::resource root_resource(GetRootResource());
  // Provide a RxSource that grabs the data from the kernel serial connection
  Console::RxSource rx_source = [root_resource = std::move(root_resource)](uint8_t* byte) {
    size_t length = 0;
    zx_status_t status =
        zx_debug_read(root_resource.get(), reinterpret_cast<char*>(byte), sizeof(*byte), &length);
    if (status == ZX_ERR_NOT_SUPPORTED) {
      // Suppress the error print in this case.  No console on this machine.
      return status;
    } else if (status != ZX_OK) {
      printf("console: error %s, length %zu from zx_debug_read syscall, exiting.\n",
             zx_status_get_string(status), length);
      return status;
    }
    if (length != 1) {
      return ZX_ERR_SHOULD_WAIT;
    }
    return ZX_OK;
  };

  Console::TxSink tx_sink = [](const uint8_t* buffer, size_t length) {
    return zx_debug_write(reinterpret_cast<const char*>(buffer), length);
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  fbl::RefPtr<Console> console;
  status = Console::Create(dispatcher, std::move(rx_source), std::move(tx_sink), &console);
  if (status != ZX_OK) {
    printf("console: Console::Create() = %s\n", zx_status_get_string(status));
    return -1;
  }

  svc::Outgoing outgoing(dispatcher);
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("console: outgoing.ServeFromStartupInfo() = %s\n", zx_status_get_string(status));
    return -1;
  }

  using Vnode =
      fs_pty::TtyService<fs_pty::SimpleConsoleOps<fbl::RefPtr<Console>>, fbl::RefPtr<Console>>;
  outgoing.svc_dir()->AddEntry("fuchsia.hardware.pty.Device",
                               fbl::AdoptRef(new Vnode(std::move(console))));

  status = loop.Run();
  ZX_ASSERT(status == ZX_OK);
  return 0;
}
