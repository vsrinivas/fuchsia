// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/bluetooth/lib/gap/adapter.h"
#include "apps/bluetooth/lib/gap/remote_device.h"
#include "apps/bluetooth/service/interfaces/common.fidl.h"
#include "apps/bluetooth/service/interfaces/control.fidl.h"

// Helpers for implementing the Bluetooth FIDL interfaces.

namespace bluetooth_service {
namespace fidl_helpers {

::bluetooth::StatusPtr NewErrorStatus(::bluetooth::ErrorCode error_code,
                                      const std::string& description);
::bluetooth::control::AdapterInfoPtr NewAdapterInfo(const ::bluetooth::gap::Adapter& adapter);
::bluetooth::control::RemoteDevicePtr NewRemoteDevice(const ::bluetooth::gap::RemoteDevice& device);

}  // namespace fidl_helpers
}  // namespace bluetooth_service
