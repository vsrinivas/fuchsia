// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_LOG_TAGS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_LOG_TAGS_H_

#include <lib/syslog/cpp/macros.h>

#include <map>
#include <string>

#include "src/developer/forensics/crash_reports/report_id.h"

namespace forensics {
namespace crash_reports {

// Stores the log tags for a report.
class LogTags {
 public:
  void Register(ReportId report_id, const std::vector<std::string>& tags);
  void Unregister(ReportId report_id);
  const char* Get(ReportId report_id);

 private:
  std::map<ReportId, std::string> tags_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_LOG_TAGS_H_
