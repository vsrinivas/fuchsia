// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/device_id_provider.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

namespace forensics {
namespace stubs {
namespace {

using Response = fuchsia::feedback::DeviceIdProvider_GetId_Response;
using Result = fuchsia::feedback::DeviceIdProvider_GetId_Result;
using Error = fuchsia::feedback::DeviceIdError;

}  // namespace

void DeviceIdProvider::GetId(GetIdCallback callback) {
  callback(Result::WithResponse(Response(std::string(device_id_))));
}

void DeviceIdProviderReturnsError::GetId(GetIdCallback callback) {
  callback(Result::WithErr(Error::NOT_FOUND));
}

DeviceIdProviderExpectsOneCall::~DeviceIdProviderExpectsOneCall() {
  FX_CHECK(!is_first_) << "Too few calls made to GetId, expecting 1 call";
}

void DeviceIdProviderExpectsOneCall::GetId(GetIdCallback callback) {
  FX_CHECK(is_first_) << "Too many calls made to GetId, expecting 1 call";
  is_first_ = false;
  callback(Result::WithResponse(Response(std::string(device_id()))));
}

void DeviceIdProviderClosesFirstConnection::GetId(GetIdCallback callback) {
  if (is_first_) {
    is_first_ = false;
    CloseConnection();
    return;
  }

  callback(Result::WithResponse(Response(std::string(device_id()))));
}

}  // namespace stubs
}  // namespace forensics
