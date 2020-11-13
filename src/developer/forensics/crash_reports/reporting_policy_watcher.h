// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORTING_POLICY_WATCHER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORTING_POLICY_WATCHER_H_

#include <fuchsia/settings/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/lib/backoff/exponential_backoff.h"

namespace forensics {
namespace crash_reports {

// The ReportingPolicy dictates how reports (and by extension their data) are handled by the crash
// reporter. The policy dictates two things: 1) when reports are deleted and 2) whether or not
// reports are eligible to be uploaded.
enum class ReportingPolicy {
  // Reports are deleted only due to space constraints and whether or not they're eligible for
  // upload is undecided.
  kUndecided,

  // Reports are deleted only due to space constraints and never eligible for upload.
  kArchive,

  // New reports are not filed and pending reports are deleted immediately.
  kDoNotFileAndDelete,

  // Reports are deleted when they are uploaded successfully or due to space constraints and are
  // always eligible for upload.
  kUpload,
};

std::string ToString(ReportingPolicy policy);

// Notifies interested parties when the component's reporting policy changes.
//
// Note: This class is inert and must inherited from to be used in a meaningful way.
class ReportingPolicyWatcher {
 public:
  virtual ~ReportingPolicyWatcher() = default;

  ReportingPolicy CurrentPolicy() const { return policy_; }

  // Register a callback that will be executed each time the reporting policy changes.
  void OnPolicyChange(::fit::function<void(ReportingPolicy)> on_change);

 protected:
  explicit ReportingPolicyWatcher(ReportingPolicy policy);

  // Set |policy_| and execute all registered callbacks if it changes.
  void SetPolicy(ReportingPolicy policy);

 private:
  ReportingPolicy policy_;
  std::vector<::fit::function<void(ReportingPolicy)>> callbacks_;
};

// A ReportingPolicyWatcher for when the reporting policy will never change.
template <ReportingPolicy policy>
class StaticReportingPolicyWatcher : public ReportingPolicyWatcher {
 public:
  StaticReportingPolicyWatcher() : ReportingPolicyWatcher(policy) {
    static_assert(policy != ReportingPolicy::kUndecided);
  }
  ~StaticReportingPolicyWatcher() override = default;
};

// A ReportingPolicyWatcher for when user consent needs to be read from the platform's privacy
// settings. In the event that the connection to the privacy settings server is lost, it will be
// assumed that the user's consent is undecided until re-connection.
//
// |fuchsia.settings.Privacy| is expected to be in |services_|.
class UserReportingPolicyWatcher : public ReportingPolicyWatcher {
 public:
  UserReportingPolicyWatcher(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services);
  ~UserReportingPolicyWatcher() override = default;

 private:
  void Watch();

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;

  backoff::ExponentialBackoff watch_backoff_;
  async::TaskClosureMethod<UserReportingPolicyWatcher, &UserReportingPolicyWatcher::Watch>
      watch_task_{this};

  fuchsia::settings::PrivacyPtr privacy_settings_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORTING_POLICY_WATCHER_H_
