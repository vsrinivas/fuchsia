// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/device_info/device_info_impl.h"

#include <string>

namespace modular {

DeviceInfoImpl::DeviceInfoImpl(const std::string& device_name,
                               const std::string& device_id,
                               const std::string& device_profile)
    : device_id_(device_id),
      device_name_(device_name),
      device_profile_(device_profile) {}

void DeviceInfoImpl::Connect(fidl::InterfaceRequest<DeviceInfo> request) {
  bindings_.AddBinding(this, std::move(request));
}

void DeviceInfoImpl::GetDeviceIdForSyncing(
    const GetDeviceIdForSyncingCallback& callback) {
  callback(device_id_);
};

void DeviceInfoImpl::GetDeviceProfile(
    const GetDeviceProfileCallback& callback) {
  callback(device_profile_);
};

void DeviceInfoImpl::GetDeviceName(const GetDeviceNameCallback& callback) {
  callback(device_name_);
}

}  // namespace modular
