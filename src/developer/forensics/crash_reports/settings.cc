// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/settings.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/crash_reports/config.h"

namespace forensics {
namespace crash_reports {

void Settings::set_upload_policy(const Settings::UploadPolicy upload_policy) {
  upload_policy_ = upload_policy;

  switch (upload_policy_) {
    case Settings::UploadPolicy::DISABLED:
      FX_LOGS(INFO) << "Upload is disabled";
      break;
    case Settings::UploadPolicy::ENABLED:
      FX_LOGS(INFO) << "Upload is enabled";
      break;
    case Settings::UploadPolicy::LIMBO:
      FX_LOGS(INFO) << "Upload is in limbo";
      break;
  }

  for (auto& watcher : upload_policy_watchers_) {
    watcher(upload_policy_);
  }
}

void Settings::set_upload_policy(const CrashServerConfig::UploadPolicy upload_policy) {
  switch (upload_policy) {
    case CrashServerConfig::UploadPolicy::DISABLED:
      set_upload_policy(UploadPolicy::DISABLED);
      break;
    case CrashServerConfig::UploadPolicy::ENABLED:
      set_upload_policy(UploadPolicy::ENABLED);
      break;
    case CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS:
      set_upload_policy(UploadPolicy::LIMBO);
      break;
  }
}

void Settings::RegisterUploadPolicyWatcher(fit::function<void(const UploadPolicy&)> watcher) {
  watcher(upload_policy_);
  upload_policy_watchers_.push_back(std::move(watcher));
}

std::string ToString(const Settings::UploadPolicy upload_policy) {
  switch (upload_policy) {
    case Settings::UploadPolicy::DISABLED:
      return "DISABLED";
    case Settings::UploadPolicy::ENABLED:
      return "ENABLED";
    case Settings::UploadPolicy::LIMBO:
      return "LIMBO";
  }
}

}  // namespace crash_reports
}  // namespace forensics
