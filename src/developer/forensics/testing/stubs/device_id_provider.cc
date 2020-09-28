// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/device_id_provider.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

namespace forensics {
namespace stubs {

void DeviceIdProvider::GetId(GetIdCallback callback) { callback(device_id_); }

DeviceIdProviderExpectsOneCall::~DeviceIdProviderExpectsOneCall() {
  FX_CHECK(!is_first_) << "Too few calls made to GetId, expecting 1 call";
}

void DeviceIdProviderExpectsOneCall::GetId(GetIdCallback callback) {
  FX_CHECK(is_first_) << "Too many calls made to GetId, expecting 1 call";
  is_first_ = false;
  callback(device_id());
}

void DeviceIdProviderClosesFirstConnection::GetId(GetIdCallback callback) {
  if (is_first_) {
    is_first_ = false;
    CloseConnection();
    return;
  }

  callback(device_id());
}

}  // namespace stubs
}  // namespace forensics
