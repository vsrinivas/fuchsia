// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_connection_client.h"

namespace magma {

std::unique_ptr<PlatformConnectionClient> PlatformConnectionClient::Create(
    uint32_t device_handle, uint32_t device_notification_handle) {
  return DRETP(nullptr, "Not implemented");
}

}  // namespace magma
