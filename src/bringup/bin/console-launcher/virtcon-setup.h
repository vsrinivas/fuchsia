// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_
#define SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_

#include <fuchsia/boot/llcpp/fidl.h>

namespace console_launcher {

zx_status_t SetupVirtcon(llcpp::fuchsia::boot::Arguments::SyncClient* boot_args);

}

#endif  // SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_VIRTCON_SETUP_H_
