// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/feedback_device_id_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/zx/time.h>

#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

FeedbackDeviceIdProvider::FeedbackDeviceIdProvider(async_dispatcher_t* dispatcher,
                                                   std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher),
      services_(services),
      pending_get_id_(dispatcher_),
      cache_id_backoff_(/*initial_delay=*/zx::msec(100), /*retry_factor=*/2u,
                        /*max_delay=*/zx::hour(1)) {
  CacheId();
}

void FeedbackDeviceIdProvider::CacheId() {
  device_id_provider_ = services_->Connect<fuchsia::feedback::DeviceIdProvider>();

  device_id_provider_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection with fuchsia.feedback.DeviceIdProvider";

    cache_id_task_.Reset([this]() { CacheId(); });
    async::PostDelayedTask(
        dispatcher_, [cache_id = cache_id_task_.callback()] { cache_id(); },
        cache_id_backoff_.GetNext());
  });

  device_id_provider_->GetId([this](fuchsia::feedback::DeviceIdProvider_GetId_Result result) {
    device_id_ = std::make_unique<std::optional<std::string>>(std::nullopt);
    if (result.is_response()) {
      *device_id_ = result.response().ResultValue_();
    }

    // Complete all of the bridges, indicating a value is now cached.
    pending_get_id_.CompleteAllOk();

    device_id_provider_.Unbind();

    // We never need to make another call nor re-connect.
    cache_id_backoff_.Reset();
    cache_id_task_.Cancel();
  });
}

fit::promise<std::string> FeedbackDeviceIdProvider::GetId(const zx::duration timeout) {
  if (device_id_) {
    return fit::make_result_promise(DeviceIdToResult());
  }

  const uint64_t id = pending_get_id_.NewBridgeForTask("Getting Feedback device id");

  return pending_get_id_.WaitForDone(id, timeout).then([id, this](const fit::result<>& result) {
    pending_get_id_.Delete(id);
    return DeviceIdToResult();
  });
}

fit::result<std::string> FeedbackDeviceIdProvider::DeviceIdToResult() {
  if (device_id_ && device_id_->has_value()) {
    return fit::ok(device_id_->value());
  }
  return fit::error();
}

}  // namespace feedback
