// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/privacy_settings_ptr.h"

#include <lib/fit/result.h>
#include <lib/fostr/fidl/fuchsia/settings/formatting.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/types.h>

#include <optional>

#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

PrivacySettingsWatcher::PrivacySettingsWatcher(std::shared_ptr<sys::ServiceDirectory> services,
                                               Settings* crash_reporter_settings)
    : services_(services), crash_reporter_settings_(crash_reporter_settings) {}

void PrivacySettingsWatcher::StartWatching() {
  Connect();
  Watch();
}

void PrivacySettingsWatcher::Connect() {
  privacy_settings_ptr_ = services_->Connect<fuchsia::settings::Privacy>();
  privacy_settings_ptr_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.settings.Privacy";
    Reset();
    // TODO(fxb/6360): re-connect with exponential backoff.
  });
}

void PrivacySettingsWatcher::Watch() {
  privacy_settings_ptr_->Watch(
      [this](fit::result<fuchsia::settings::PrivacySettings, fuchsia::settings::Error> result) {
        if (result.is_error()) {
          FX_LOGS(ERROR) << "Failed to obtain privacy settings: " << result.error();
          Reset();
        } else {
          privacy_settings_ = result.take_value();
          Update();
        }

        // We watch for the next update, following the hanging get pattern.
        Watch();
      });
}

void PrivacySettingsWatcher::Reset() {
  privacy_settings_.clear_user_data_sharing_consent();
  Update();
}

void PrivacySettingsWatcher::Update() {
  if (!privacy_settings_.has_user_data_sharing_consent()) {
    crash_reporter_settings_->set_upload_policy(Settings::UploadPolicy::LIMBO);
  } else if (privacy_settings_.user_data_sharing_consent()) {
    crash_reporter_settings_->set_upload_policy(Settings::UploadPolicy::ENABLED);
  } else {
    crash_reporter_settings_->set_upload_policy(Settings::UploadPolicy::DISABLED);
  }
}

}  // namespace feedback
