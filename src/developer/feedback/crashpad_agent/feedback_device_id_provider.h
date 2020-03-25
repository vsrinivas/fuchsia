// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string>

#include "src/developer/feedback/utils/weak_bridge.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Wraps around fuchsia::feedback::DeviceIdProviderPtr to handle establishing the connection, losing
// the connection, waiting for the callback, enforcing a timeout, etc.
class FeedbackDeviceIdProvider {
 public:
  FeedbackDeviceIdProvider(async_dispatcher_t* dispatcher,
                           std::shared_ptr<sys::ServiceDirectory> services);

  fit::promise<std::string> GetId(zx::duration timeout);

 private:
  void CacheId();

  // Turns the cached device id into a fit::result::ok() if an actual ID is cached, error()
  // otherwise.
  fit::result<std::string> DeviceIdToResult();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::feedback::DeviceIdProviderPtr device_id_provider_;

  // The std::unique_ptr<> indicates whether the value is cached, the std::optional<> indicates
  // whether the cached value is an actual id.
  std::unique_ptr<std::optional<std::string>> device_id_;

  // We use a WeakBridge because the fit::bridge can be invalidated through
  // several flows, including a delayed task on the dispatcher, which outlives this class.
  std::map<uint64_t, WeakBridge<>> pending_get_id_;
  uint64_t next_get_id_ = 1;

  // We need to be able to cancel a posted retry task when |this| is destroyed.
  fxl::CancelableClosure cache_id_task_;
  backoff::ExponentialBackoff cache_id_backoff_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FeedbackDeviceIdProvider);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DEVICE_ID_PROVIDER_H_
