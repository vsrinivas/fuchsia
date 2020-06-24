// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SETTINGS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SETTINGS_H_

#include <lib/fit/function.h>

#include <vector>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace crash_reports {

// Crash reporter settings.
//
// It is intended to represent a mutable set of settings as opposed to the immutable Config.
class Settings {
 public:
  Settings() {}

  // Policy defining whether to upload pending and future crash reports to a remote crash server.
  enum class UploadPolicy {
    // Crash reports should (1) not be uploaded and (2) marked as completed in the Crashpad database
    // to avoid trying to ever upload them in the future.
    DISABLED,

    // Crash reports should be uploaded and on success marked as completed in the Crashpad database.
    // If the upload is unsuccessful and the policy changes to DISABLED, the crash report should
    // follow the DISABLED policy.
    ENABLED,

    // Crash reports should stay pending until a change in policy to either DISABLED or ENABLED.
    LIMBO,
  };

  const UploadPolicy& upload_policy() { return upload_policy_; }
  void set_upload_policy(UploadPolicy upload_policy);
  void set_upload_policy(CrashServerConfig::UploadPolicy upload_policy);

  // The watcher will be immediately called with the current upload policy and then called whenever
  // the upload policy changes.
  void RegisterUploadPolicyWatcher(fit::function<void(const UploadPolicy&)> watcher);

 private:
  UploadPolicy upload_policy_ = UploadPolicy::LIMBO;
  std::vector<fit::function<void(const UploadPolicy&)>> upload_policy_watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Settings);
};

// Returns the string version of the enum.
std::string ToString(Settings::UploadPolicy upload_policy);

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SETTINGS_H_
