// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/settings.h"

#include <lib/syslog/cpp/logger.h>

namespace feedback {

void Settings::set_upload_policy(const Settings::UploadPolicy upload_policy) {
  upload_policy_ = upload_policy;

  switch (upload_policy_) {
    case Settings::UploadPolicy::DISABLED:
      FX_LOGS(INFO) << "Crash report upload is disabled";
      break;
    case Settings::UploadPolicy::ENABLED:
      FX_LOGS(INFO) << "Crash report upload is enabled";
      break;
    case Settings::UploadPolicy::LIMBO:
      FX_LOGS(INFO) << "Crash report upload is in limbo";
      break;
  }
}

void Settings::set_upload_policy(const std::optional<bool> enabled) {
  if (!enabled.has_value()) {
    set_upload_policy(UploadPolicy::LIMBO);
  } else if (enabled.value()) {
    set_upload_policy(UploadPolicy::ENABLED);
  } else {
    set_upload_policy(UploadPolicy::DISABLED);
  }
}

}  // namespace feedback
