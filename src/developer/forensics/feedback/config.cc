// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/config.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"

namespace forensics::feedback {

std::optional<crash_reports::Config> GetCrashReportsConfig(const std::string& default_path,
                                                           const std::string& override_path) {
  std::optional<crash_reports::Config> config;
  if (files::IsFile(override_path)) {
    if (config = crash_reports::ParseConfig(override_path); !config) {
      FX_LOGS(ERROR) << "Failed to read override config file at " << override_path
                     << " - falling back to default config file";
    }
  }

  if (!config) {
    if (config = crash_reports::ParseConfig(default_path); !config) {
      FX_LOGS(ERROR) << "Failed to read default config file at " << default_path;
    }
  }

  return config;
}

std::optional<feedback_data::Config> GetFeedbackDataConfig(const std::string& path) {
  feedback_data::Config config;
  if (const zx_status_t status = feedback_data::ParseConfig(path, &config); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read config file at " << path;
    return std::nullopt;
  }

  return config;
}

}  // namespace forensics::feedback
