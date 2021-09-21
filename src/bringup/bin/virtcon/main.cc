// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/service/llcpp/service.h>
#include <lib/svc/dir.h>
#include <lib/svc/outgoing.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/object.h>

#include <iterator>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "args.h"
#include "keyboard.h"
#include "session-manager.h"
#include "src/lib/listnode/listnode.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "vc.h"

int main(int argc, char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    FX_LOGS(ERROR)
        << "Failed to redirect stdout to debuglog, assuming test environment and continuing";
  }

  fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args;
  {
    auto client = service::Connect<fuchsia_boot::Arguments>();
    if (client.is_error()) {
      fprintf(stderr, "vc: failed to connect to fuchsia.boot.Arguments\n");
      return 1;
    }
    boot_args = fidl::BindSyncClient(std::move(*client));
  }

  Arguments args;
  status = ParseArgs(boot_args, &args);
  if (status != ZX_OK) {
    printf("vc: failed to get boot arguments\n");
    return -1;
  }

  if (args.disable) {
    printf("vc: virtcon disabled\n");
    return 0;
  }

  vc_device_init(args.font, args.keymap);

  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);

  virtcon::SessionManager virtcon_server =
      virtcon::SessionManager(loop.dispatcher(), args.keep_log_visible, args.color_scheme);

  svc::Outgoing outgoing(loop.dispatcher());
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("vc: outgoing.ServeFromStartupInfo() = %s\n", zx_status_get_string(status));
    return -1;
  }
  status = outgoing.svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_virtualconsole::SessionManager>,
      fbl::MakeRefCounted<fs::Service>(
          [&virtcon_server](
              fidl::ServerEnd<fuchsia_virtualconsole::SessionManager> request) mutable {
            virtcon_server.Bind(std::move(request));
            return ZX_OK;
          }));

  {
    auto local = service::Connect<fuchsia_boot::ReadOnlyLog>();
    if (local.is_error()) {
      fprintf(stderr, "vc: unable to connect to fuchsia.boot.ReadOnlyLog %d\n", status);
      return -1;
    }

    auto result = BindSyncClient(std::move(*local)).Get();
    if (!result.ok()) {
      fprintf(stderr, "vc: unable to get read only debulog\n");
      return -1;
    }

    if (log_start(loop.dispatcher(), std::move(result->log), args.color_scheme) < 0) {
      fprintf(stderr, "vc: log_start failed\n");
      return -1;
    }
  }

  if (!args.repeat_keys) {
    printf("vc: Key repeat disabled\n");
  }

  status = setup_keyboard_watcher(loop.dispatcher(), handle_key_press, args.repeat_keys);
  if (status != ZX_OK) {
    printf("vc: setup_keyboard_watcher failed with %d\n", status);
  }

  if (!vc_sysmem_connect()) {
    fprintf(stderr, "vc: failed to connect to sysmem\n");
    return -1;
  }

  if (!vc_display_init(loop.dispatcher(), args.hide_on_boot)) {
    fprintf(stderr, "vc: failed to initialize display\n");
    return -1;
  }

  printf("vc: started\n");
  status = loop.Run();
  printf("vc: loop stopped: %d\n", status);
  return -1;
}
