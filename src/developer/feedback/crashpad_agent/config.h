// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONFIG_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONFIG_H_

#include <zircon/types.h>

#include <memory>
#include <string>

namespace feedback {

struct CrashServerConfig {
  // Policy defining whether to upload pending and future crash reports to a remote crash server.
  enum class UploadPolicy {
    // Crash reports should (1) not be uploaded and (2) marked as completed in the Crashpad database
    // to avoid trying to ever upload them in the future.
    DISABLED,

    // Crash reports should be uploaded and on success marked as completed in the Crashpad database.
    // If the upload is unsuccessful and the policy changes to DISABLED, the crash report should
    // follow the DISABLED policy.
    ENABLED,

    // Policy should not be read from the config, but instead from the privacy settings.
    READ_FROM_PRIVACY_SETTINGS,
  };
  UploadPolicy upload_policy = UploadPolicy::DISABLED;

  // URL of the remote crash server.
  //
  // We use a std::unique_ptr to set it only when relevant, i.e. when the policy is not DISABLED.
  std::unique_ptr<std::string> url;
};

// Crash reporter static configuration.
//
// It is intended to represent an immutable configuration, typically loaded from a file.
struct Config {
  CrashServerConfig crash_server;
};

// Parses the JSON config at |filepath| as |config|.
zx_status_t ParseConfig(const std::string& filepath, Config* config);

// Returns the string version of the enum.
std::string ToString(CrashServerConfig::UploadPolicy upload_policy);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONFIG_H_
