// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_SVCHOST_ARGS_H_
#define SRC_BRINGUP_BIN_SVCHOST_ARGS_H_

#include <fuchsia/boot/llcpp/fidl.h>

namespace svchost {

struct Arguments {
  bool require_system = false;
};

zx_status_t ParseArgs(llcpp::fuchsia::boot::Arguments::SyncClient& client, Arguments* out);

}  // namespace svchost

#endif  // SRC_BRINGUP_BIN_SVCHOST_ARGS_H_
