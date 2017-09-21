// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/bluetooth/fidl/common.fidl.h"
#include "lib/bluetooth/fidl/control.fidl.h"

namespace bluetoothcli {

std::string AppearanceToString(bluetooth::control::Appearance appearance);
std::string TechnologyTypeToString(bluetooth::control::TechnologyType type);
std::string ErrorCodeToString(bluetooth::ErrorCode error_code);
std::string BoolToString(bool val);

void PrintAdapterInfo(const bluetooth::control::AdapterInfoPtr& adapter_info, size_t indent = 0u);
void PrintRemoteDevice(const bluetooth::control::RemoteDevicePtr& remote_device,
                       size_t indent = 0u);

}  // namespace bluetoothcli
