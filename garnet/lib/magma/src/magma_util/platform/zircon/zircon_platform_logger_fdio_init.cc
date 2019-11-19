// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include "zircon_platform_handle.h"
#include "zircon_platform_logger.h"

namespace magma {

bool ZirconPlatformLogger::InitWithFdio() {
  zx::channel client_channel, server_channel;
  zx_status_t status = zx::channel::create(0, &client_channel, &server_channel);
  if (status != ZX_OK)
    return false;

  status = fdio_service_connect("/svc/fuchsia.logger.LogSink", server_channel.release());
  if (status != ZX_OK)
    return false;

  return PlatformLogger::Initialize(
      std::make_unique<magma::ZirconPlatformHandle>(std::move(client_channel)));
}

}  // namespace magma
