// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/fakes/fake_device_id_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include <memory>
#include <optional>

#include "src/lib/uuid/uuid.h"

namespace feedback {

using namespace fuchsia::feedback;

void FakeDeviceIdProvider::GetId(GetIdCallback callback) {
  if (!device_id_) {
    device_id_ = std::make_unique<std::optional<std::string>>(std::nullopt);
    std::string uuid = uuid::Generate();
    if (uuid::IsValid(uuid)) {
      *device_id_ = uuid;
    }
  }

  DeviceIdProvider_GetId_Result result;
  if (device_id_->has_value()) {
    // We need to copy |device_id_| since Response::Response() requires a rvalue reference to
    // std::string in its constructor.
    DeviceIdProvider_GetId_Response response(std::string(device_id_->value()));
    result = DeviceIdProvider_GetId_Result::WithResponse(std::move(response));
  } else {
    result = DeviceIdProvider_GetId_Result::WithErr(DeviceIdError::NOT_FOUND);
  }

  callback(std::move(result));
}

}  // namespace feedback
