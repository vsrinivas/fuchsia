// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/fidl/device_id_provider_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/zx/time.h>

#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace fidl {

DeviceIdProviderPtr::DeviceIdProviderPtr(async_dispatcher_t* dispatcher,
                                         std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher),
      services_(services),
      pending_calls_(dispatcher_),
      cache_id_backoff_(/*initial_delay=*/zx::msec(100), /*retry_factor=*/2u,
                        /*max_delay=*/zx::hour(1)) {
  CacheId();
}

void DeviceIdProviderPtr::CacheId() {
  connection_ = services_->Connect<fuchsia::feedback::DeviceIdProvider>();

  connection_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection with fuchsia.feedback.DeviceIdProvider";

    cache_id_task_.Reset([this]() { CacheId(); });
    async::PostDelayedTask(
        dispatcher_, [cache_id = cache_id_task_.callback()] { cache_id(); },
        cache_id_backoff_.GetNext());
  });

  connection_->GetId([this](fuchsia::feedback::DeviceIdProvider_GetId_Result result) {
    device_id_ = std::make_unique<std::optional<std::string>>(std::nullopt);
    if (result.is_response()) {
      *device_id_ = result.response().ResultValue_();
    }

    // Complete all of the bridges, indicating a value is now cached.
    pending_calls_.CompleteAllOk();

    connection_.Unbind();

    // We never need to make another call nor re-connect.
    cache_id_backoff_.Reset();
    cache_id_task_.Cancel();
  });
}

fit::promise<std::string> DeviceIdProviderPtr::GetId(const zx::duration timeout) {
  if (device_id_) {
    return fit::make_result_promise(DeviceIdToResult());
  }

  const uint64_t id = pending_calls_.NewBridgeForTask("Getting Feedback device id");

  return pending_calls_.WaitForDone(id, timeout).then([id, this](const fit::result<>& result) {
    pending_calls_.Delete(id);
    return DeviceIdToResult();
  });
}

fit::result<std::string> DeviceIdProviderPtr::DeviceIdToResult() {
  if (device_id_ && device_id_->has_value()) {
    return fit::ok(device_id_->value());
  }
  return fit::error();
}

}  // namespace fidl
}  // namespace feedback
