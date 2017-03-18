// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/device_info/device_info_impl.h"

#include "apps/modular/lib/device_info/device_info.h"

#include <string>

namespace modular {

DeviceInfoImpl::DeviceInfoImpl(const std::string& user)
    : modular::DeviceInfo(),
      device_id_(LoadDeviceID(user)),
      device_profile_(LoadDeviceProfile()) {}

void DeviceInfoImpl::AddBinding(fidl::InterfaceRequest<DeviceInfo> request) {
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


}  // namespace modular
