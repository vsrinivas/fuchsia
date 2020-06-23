// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/fidl/device_id_provider_ptr.h"

#include <utility>

#include "src/developer/feedback/utils/errors.h"

namespace forensics {
namespace fidl {

DeviceIdProviderPtr::DeviceIdProviderPtr(async_dispatcher_t* dispatcher,
                                         std::shared_ptr<sys::ServiceDirectory> services)
    : connection_(dispatcher, services, [this] { MakeCall(); }) {}

::fit::promise<std::string, Error> DeviceIdProviderPtr::GetId(const zx::duration timeout) {
  return connection_.GetValue(fit::Timeout(timeout));
}

void DeviceIdProviderPtr::MakeCall() {
  connection_->GetId([this](fuchsia::feedback::DeviceIdProvider_GetId_Result result) {
    if (result.is_response()) {
      connection_.SetValue(result.response().ResultValue_());
    } else {
      connection_.SetError(Error::kMissingValue);
    }
  });
}

}  // namespace fidl
}  // namespace forensics
