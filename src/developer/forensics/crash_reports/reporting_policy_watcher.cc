// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/reporting_policy_watcher.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace crash_reports {

std::string ToString(const ReportingPolicy policy) {
  switch (policy) {
    case ReportingPolicy::kUndecided:
      return "UNDECIDED";
    case ReportingPolicy::kArchive:
      return "ARCHIVE";
    case ReportingPolicy::kDoNotFileAndDelete:
      return "DO_NOT_FILE_AND_DELETE";
    case ReportingPolicy::kUpload:
      return "UPLOAD";
  }
}

ReportingPolicyWatcher::ReportingPolicyWatcher(const ReportingPolicy policy) : policy_(policy) {}

void ReportingPolicyWatcher::OnPolicyChange(::fit::function<void(ReportingPolicy)> on_change) {
  callbacks_.push_back(std::move(on_change));
}

void ReportingPolicyWatcher::SetPolicy(const ReportingPolicy policy) {
  if (policy_ == policy) {
    return;
  }

  policy_ = policy;
  for (const auto& on_change : callbacks_) {
    on_change(policy_);
  }
}

UserReportingPolicyWatcher::UserReportingPolicyWatcher(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services)
    : ReportingPolicyWatcher(ReportingPolicy::kUndecided),
      dispatcher_(dispatcher),
      services_(std::move(services)),
      watch_backoff_(/*initial_delay=*/zx::min(1), /*retry_factor=*/2u, /*max_delay=*/zx::hour(1)) {
  privacy_settings_.set_error_handler([this](const zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.settings.Privacy";
    SetPolicy(ReportingPolicy::kUndecided);
    watch_task_.PostDelayed(dispatcher_, watch_backoff_.GetNext());
  });

  Watch();
}

void UserReportingPolicyWatcher::Watch() {
  if (!privacy_settings_.is_bound()) {
    services_->Connect(privacy_settings_.NewRequest());
  }

  privacy_settings_->Watch([this](const fuchsia::settings::PrivacySettings settings) {
    watch_backoff_.Reset();

    if (!settings.has_user_data_sharing_consent()) {
      SetPolicy(ReportingPolicy::kUndecided);
    } else if (settings.user_data_sharing_consent()) {
      SetPolicy(ReportingPolicy::kUpload);
    } else {
      SetPolicy(ReportingPolicy::kDoNotFileAndDelete);
    }

    // Watch for the next update, following the hanging-get pattern.
    Watch();
  });
}

}  // namespace crash_reports
}  // namespace forensics
