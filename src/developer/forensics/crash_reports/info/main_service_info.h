// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_MAIN_SERVICE_INFO_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_MAIN_SERVICE_INFO_H_

#include <memory>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/utils/inspect_protocol_stats.h"

namespace forensics {
namespace crash_reports {

// Information about the agent we want to export.
struct MainServiceInfo {
 public:
  MainServiceInfo(std::shared_ptr<InfoContext> context);

  // Exposes the static configuration of the agent.
  void ExposeConfig(const Config& config);

 private:
  std::shared_ptr<InfoContext> context_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_MAIN_SERVICE_INFO_H_
