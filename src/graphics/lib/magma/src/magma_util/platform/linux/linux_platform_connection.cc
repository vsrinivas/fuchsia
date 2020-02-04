// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_connection.h"

namespace magma {

std::shared_ptr<PlatformConnection> PlatformConnection::Create(std::unique_ptr<Delegate> delegate,
                                                               msd_client_id_t client_id) {
  return std::make_shared<LinuxPlatformConnection>(std::move(delegate), client_id);
}

}  // namespace magma
