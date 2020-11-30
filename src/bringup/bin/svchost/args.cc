// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/svchost/args.h"

namespace svchost {

zx_status_t ParseArgs(llcpp::fuchsia::boot::Arguments::SyncClient& client, Arguments* out) {
  auto result = client.GetBool(fidl::StringView{"devmgr.require-system"}, false);
  if (!result.ok()) {
    return result.status();
  }
  out->require_system = result->value;
  return ZX_OK;
}

}  // namespace svchost
