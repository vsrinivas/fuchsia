// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>

#include <cstdio>
#include <string>

namespace hwstress {

// Open the given path as a FIDL channel.
zx::result<zx::channel> OpenDeviceChannel(std::string_view path) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = fdio_service_connect(std::string(path).c_str(), server.release());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::success(std::move(client));
}

}  // namespace hwstress
