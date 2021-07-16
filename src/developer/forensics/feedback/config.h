// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_CONFIG_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_CONFIG_H_

#include <optional>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/feedback/constants.h"

namespace forensics::feedback {

std::optional<crash_reports::Config> GetCrashReportsConfig(
    const std::string& default_path = kDefaultCrashReportsConfigPath,
    const std::string& override_path = kOverrideCrashReportsConfigPath);

}

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_CONFIG_H_
