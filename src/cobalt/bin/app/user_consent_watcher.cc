// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/user_consent_watcher.h"

#include <lib/fostr/fidl/fuchsia/settings/formatting.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <optional>

#include "lib/async/cpp/task.h"

namespace cobalt {

UserConsentWatcher::UserConsentWatcher(
    async_dispatcher_t *dispatcher, inspect::Node inspect_node,
    std::shared_ptr<sys::ServiceDirectory> services,
    std::function<void(const CobaltServiceInterface::DataCollectionPolicy &)> callback)
    : dispatcher_(dispatcher),
      inspect_node_(std::move(inspect_node)),
      services_(services),
      callback_(callback),
      backoff_(/*initial_delay=*/zx::msec(100), /*retry_factor=*/2u, /*max_delay=*/zx::hour(1)) {
  watch_successes_ = inspect_node_.CreateInt("successful_watches", 0);
  watch_errors_ = inspect_node_.CreateInt("watch_errors", 0);
}

void UserConsentWatcher::StartWatching() {
  privacy_settings_ptr_ = services_->Connect<fuchsia::settings::Privacy>();
  privacy_settings_ptr_.set_error_handler([this](zx_status_t status) {
    watch_errors_.Add(1);
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.settings.Privacy";
    RestartWatching();
  });

  Watch();
}

void UserConsentWatcher::RestartWatching() {
  ResetConsent();
  privacy_settings_ptr_.Unbind();

  reconnect_task_.Reset([this] { StartWatching(); });
  async::PostDelayedTask(
      dispatcher_, [reconnect = reconnect_task_.callback()] { reconnect(); }, backoff_.GetNext());
}

void UserConsentWatcher::Watch() {
  privacy_settings_ptr_->Watch([this](fuchsia::settings::PrivacySettings settings) {
    // Reset the exponential backoff since we successfully watched once.
    backoff_.Reset();

    watch_successes_.Add(1);
    privacy_settings_ = std::move(settings);
    Update();

    // We watch for the next update, following the hanging get pattern.
    Watch();
  });
}

void UserConsentWatcher::ResetConsent() {
  privacy_settings_.clear_user_data_sharing_consent();
  Update();
}

CobaltServiceInterface::DataCollectionPolicy UserConsentWatcher::GetDataCollectionPolicy() {
  if (!privacy_settings_.has_user_data_sharing_consent()) {
    return CobaltServiceInterface::DataCollectionPolicy::DO_NOT_UPLOAD;
  }
  if (privacy_settings_.user_data_sharing_consent()) {
    return CobaltServiceInterface::DataCollectionPolicy::COLLECT_AND_UPLOAD;
  }
  return CobaltServiceInterface::DataCollectionPolicy::DO_NOT_COLLECT;
}

void UserConsentWatcher::Update() {
  CobaltServiceInterface::DataCollectionPolicy policy = GetDataCollectionPolicy();
  current_policy_ = inspect_node_.CreateInt("data_collection_policy", static_cast<int>(policy));
  callback_(policy);
}

}  // namespace cobalt
