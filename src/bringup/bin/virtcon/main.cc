// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
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
#include <fbl/string_piece.h>
#include <fbl/unique_fd.h>
#include <fs/service.h>
#include <src/storage/deprecated-fs-fidl-handler/fidl-handler.h>

#include "args.h"
#include "keyboard.h"
#include "session-manager.h"
#include "src/lib/listnode/listnode.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "vc.h"

int main(int argc, char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    FX_LOGS(ERROR)
        << "Failed to redirect stdout to debuglog, assuming test environment and continuing";
  }

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  {
    zx::channel local, remote;
    status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return 1;
    }
    status = fdio_service_connect("/svc/fuchsia.boot.Arguments", remote.release());
    if (status != ZX_OK) {
      fprintf(stderr, "vc: failed to connect to fuchsia.boot.Arguments\n");
      return 1;
    }
    boot_args = llcpp::fuchsia::boot::Arguments::SyncClient(std::move(local));
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
      llcpp::fuchsia::virtualconsole::SessionManager::Name,
      fbl::MakeRefCounted<fs::Service>([&virtcon_server](zx::channel request) mutable {
        zx_status_t status = virtcon_server.Bind(std::move(request));
        if (status != ZX_OK) {
          printf("vc: error binding new server: %d\n", status);
        }
        return status;
      }));

  {
    zx::channel local, remote;
    status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return -1;
    }

    status = fdio_service_connect("/svc/fuchsia.boot.ReadOnlyLog", remote.release());
    if (status != ZX_OK) {
      fprintf(stderr, "vc: unable to connect to fuchsia.boot.ReadOnlyLog %d\n", status);
      return 1;
    }

    auto result = llcpp::fuchsia::boot::ReadOnlyLog::Call::Get(zx::unowned(local));
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
