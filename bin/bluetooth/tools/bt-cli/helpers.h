// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bluetooth/cpp/fidl.h>
#include <bluetooth_control/cpp/fidl.h>

namespace bluetoothcli {

std::string AppearanceToString(bluetooth_control::Appearance appearance);
std::string TechnologyTypeToString(bluetooth_control::TechnologyType type);
std::string ErrorCodeToString(bluetooth::ErrorCode error_code);
std::string BoolToString(bool val);

void PrintAdapterInfo(const bluetooth_control::AdapterInfo& adapter_info,
                      size_t indent = 0u);
void PrintRemoteDevice(const bluetooth_control::RemoteDevice& remote_device,
                       size_t indent = 0u);

}  // namespace bluetoothcli
