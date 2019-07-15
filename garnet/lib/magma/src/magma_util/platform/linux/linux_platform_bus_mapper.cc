// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_bus_mapper.h"

namespace magma {

std::unique_ptr<PlatformBusMapper> PlatformBusMapper::Create(
    std::shared_ptr<PlatformHandle> bus_transaction_initiator) {
  return DRETP(nullptr, "Not implemented");
}

}  // namespace magma
