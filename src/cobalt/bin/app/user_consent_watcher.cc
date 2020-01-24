// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/user_consent_watcher.h"

#include <lib/fit/result.h>
#include <lib/fostr/fidl/fuchsia/settings/formatting.h>
#include <zircon/types.h>

#include <optional>

#include "lib/async/cpp/task.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace cobalt {

UserConsentWatcher::UserConsentWatcher(
    async_dispatcher_t *dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    std::function<void(const CobaltService::DataCollectionPolicy &)> callback)
    : dispatcher_(dispatcher),
      services_(services),
      callback_(callback),
      backoff_(/*initial_delay=*/zx::msec(100), /*retry_factor=*/2u, /*max_delay=*/zx::hour(1)) {}

void UserConsentWatcher::StartWatching() {
  privacy_settings_ptr_ = services_->Connect<fuchsia::settings::Privacy>();
  privacy_settings_ptr_.set_error_handler([this](zx_status_t status) {
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
  privacy_settings_ptr_->Watch(
      [this](fit::result<fuchsia::settings::PrivacySettings, fuchsia::settings::Error> result) {
        if (result.is_error()) {
          FX_LOGS(ERROR) << "Failed to obtain privacy settings: " << result.error();
          RestartWatching();
          return;
        }

        // Reset the exponential backoff since we successfully watched once.
        backoff_.Reset();

        privacy_settings_ = result.take_value();
        Update();

        // We watch for the next update, following the hanging get pattern.
        Watch();
      });
}

void UserConsentWatcher::ResetConsent() {
  privacy_settings_.clear_user_data_sharing_consent();
  Update();
}

void UserConsentWatcher::Update() {
  if (!privacy_settings_.has_user_data_sharing_consent()) {
    callback_(CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD);
  } else if (privacy_settings_.user_data_sharing_consent()) {
    callback_(CobaltService::DataCollectionPolicy::COLLECT_AND_UPLOAD);
  } else {
    callback_(CobaltService::DataCollectionPolicy::DO_NOT_COLLECT);
  }
}

}  // namespace cobalt
