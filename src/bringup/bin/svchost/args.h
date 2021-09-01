// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_SVCHOST_ARGS_H_
#define SRC_BRINGUP_BIN_SVCHOST_ARGS_H_

#include <fidl/fuchsia.boot/cpp/wire.h>

namespace svchost {

struct Arguments {
  bool require_system = false;
};

zx_status_t ParseArgs(fidl::WireSyncClient<fuchsia_boot::Arguments>& client, Arguments* out);

}  // namespace svchost

#endif  // SRC_BRINGUP_BIN_SVCHOST_ARGS_H_
