// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/device_id_provider.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

namespace forensics {
namespace stubs {

void DeviceIdProviderBase::GetId(GetIdCallback callback) { GetIdInternal(std::move(callback)); }

void DeviceIdProviderBase::GetIdInternal(GetIdCallback callback) {
  callback_ = std::move(callback);
  if (!dirty_) {
    dirty_ = true;
  } else {
    FX_CHECK(device_id_.has_value());
    callback_(device_id_.value());
    dirty_ = false;
  }
}

void DeviceIdProviderBase::SetDeviceId(std::string device_id) {
  device_id_ = std::move(device_id);
  if (dirty_ && callback_) {
    callback_(device_id_.value());
  }
  dirty_ = false;
}

void DeviceIdProvider::GetId(GetIdCallback callback) { GetIdInternal(std::move(callback)); }

DeviceIdProviderExpectsOneCall::~DeviceIdProviderExpectsOneCall() {
  FX_CHECK(!is_first_) << "Too few calls made to GetId, expecting 1 call";
}

void DeviceIdProviderExpectsOneCall::GetId(GetIdCallback callback) {
  FX_CHECK(is_first_) << "Too many calls made to GetId, expecting 1 call";
  is_first_ = false;
  GetIdInternal(std::move(callback));
}

void DeviceIdProviderClosesFirstConnection::GetId(GetIdCallback callback) {
  if (is_first_) {
    is_first_ = false;
    CloseConnection();
    return;
  }

  GetIdInternal(std::move(callback));
}

}  // namespace stubs
}  // namespace forensics
