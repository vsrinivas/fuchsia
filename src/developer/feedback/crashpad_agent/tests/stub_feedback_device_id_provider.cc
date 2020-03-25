// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/stub_feedback_device_id_provider.h"

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace {

using Response = fuchsia::feedback::DeviceIdProvider_GetId_Response;
using Result = fuchsia::feedback::DeviceIdProvider_GetId_Result;
using Error = fuchsia::feedback::DeviceIdError;

}  // namespace

void StubFeedbackDeviceIdProvider::CloseConnection() {
  if (binding_) {
    binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void StubFeedbackDeviceIdProvider::GetId(GetIdCallback callback) {
  callback(Result::WithResponse(Response(std::string(device_id_))));
}

void StubFeedbackDeviceIdProviderReturnsError::GetId(GetIdCallback callback) {
  callback(Result::WithErr(Error::NOT_FOUND));
}

void StubFeedbackDeviceIdProviderNeverReturns::GetId(GetIdCallback callback) {}

StubFeedbackDeviceIdProviderExpectsOneCall::~StubFeedbackDeviceIdProviderExpectsOneCall() {
  FXL_CHECK(!is_first_) << "Too few calls made to GetId, expecting 1 call";
}

void StubFeedbackDeviceIdProviderExpectsOneCall::GetId(GetIdCallback callback) {
  FXL_CHECK(is_first_) << "Too many calls made to GetId, expecting 1 call";
  is_first_ = false;
  callback(Result::WithResponse(Response(std::string(device_id()))));
}

void StubFeedbackDeviceIdProviderClosesFirstConnection::GetId(GetIdCallback callback) {
  if (is_first_) {
    is_first_ = false;
    CloseConnection();
    return;
  }

  callback(Result::WithResponse(Response(std::string(device_id()))));
}

}  // namespace feedback
