// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONFIG_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONFIG_H_

#include <zircon/types.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace forensics {
namespace crash_reports {

// Crash reporter static configuration.
//
// It is intended to represent an immutable configuration, typically loaded from a file.
//
// TODO(fxbug.dev/111700): Move these value to feedback::BuildTypeConfig.
struct Config {
  // Policy defining whether to upload pending and future crash reports to a remote crash server.
  enum class UploadPolicy {
    // Crash reports should not be uploaded and be kept in the store.
    kDisabled,

    // Crash reports should be uploaded and on success removed from the store, if present.
    // If the upload is unsuccessful and the policy changes to kDisabled, the crash report should
    // follow the kDisabled policy.
    kEnabled,

    // Policy should not be read from the config, but instead from the privacy settings.
    kReadFromPrivacySettings,
  };

  UploadPolicy crash_report_upload_policy;
  std::optional<uint64_t> daily_per_product_quota;
};

// Parses the JSON config at |filepath|. Returns std::nullopt if an error occurs.
std::optional<Config> ParseConfig(const std::string& filepath);

// Returns the string version of the enum.
std::string ToString(Config::UploadPolicy upload_policy);

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONFIG_H_
