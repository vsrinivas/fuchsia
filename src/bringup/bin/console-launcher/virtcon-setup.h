// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_
#define SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.virtualconsole/cpp/wire.h>
#include <lib/zx/status.h>

namespace console_launcher {

struct VirtconArgs {
  bool should_launch = false;
  bool need_debuglog = false;
};
zx::status<VirtconArgs> GetVirtconArgs(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args);

zx_status_t SetupVirtconEtc(fidl::WireSyncClient<fuchsia_virtualconsole::SessionManager>& virtcon,
                            const VirtconArgs& args);

zx_status_t SetupVirtcon(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args);

}  // namespace console_launcher

#endif  // SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_
