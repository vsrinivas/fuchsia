// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_
#define SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_

#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/virtualconsole/llcpp/fidl.h>
#include <lib/zx/status.h>

namespace console_launcher {

struct VirtconArgs {
  bool should_launch = false;
  bool need_debuglog = false;
};
zx::status<VirtconArgs> GetVirtconArgs(llcpp::fuchsia::boot::Arguments::SyncClient* boot_args);

zx_status_t SetupVirtconEtc(llcpp::fuchsia::virtualconsole::SessionManager::SyncClient& virtcon,
                            const VirtconArgs& args);

zx_status_t SetupVirtcon(llcpp::fuchsia::boot::Arguments::SyncClient* boot_args);

}  // namespace console_launcher

#endif  // SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_
